#include "neo_cppjieba.h"

#include "neo/Config.hpp"
#include "neo/Jieba.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// The opaque engine owns the existing facade without introducing shared scratch buffers.
struct neo_jieba_engine_v1 {
    explicit neo_jieba_engine_v1(const neo_jieba_paths_v1 &paths)
        : jieba(paths.dict_path, paths.model_path, paths.user_dict_path) {
    }

    neo_cppjieba::Jieba jieba;
};

// No stored pointers refer to the engine, caller input, or a temporary decoding workspace.
struct neo_jieba_tokens_v1 {
    std::string text;
    std::vector<neo_jieba_token_range_v1> ranges;
};

namespace {

constexpr auto max_input_bytes =
    std::min({static_cast<uint64_t>(std::numeric_limits<neo_cppjieba::SourceOffset>::max()),
              static_cast<uint64_t>(std::numeric_limits<neo_cppjieba::RuneIndex>::max()),
              static_cast<uint64_t>(std::numeric_limits<std::size_t>::max() - 1),
              static_cast<uint64_t>(std::numeric_limits<std::ptrdiff_t>::max() - 1)});

static_assert(sizeof(neo_jieba_token_range_v1) == 2 * sizeof(uint64_t));
static_assert(offsetof(neo_jieba_token_range_v1, byte_length) == sizeof(uint64_t));

// Error reporting never allocates, including when handling allocation failures.
auto report_error(neo_jieba_error_buffer_v1 *error, neo_jieba_status_v1 status, std::string_view message) noexcept
    -> neo_jieba_status_v1 {
    if (error != nullptr) {
        error->message_size = message.size();
        if (error->capacity > 0) {
            const auto count = std::min(message.size(), static_cast<std::size_t>(error->capacity - 1));
            std::copy_n(message.data(), count, error->data);
            error->data[count] = '\0';
        }
    }
    return status;
}

// Validate diagnostic storage before any writes; unknown exceptions indicate an internal failure.
template <typename Operation>
auto invoke_boundary(neo_jieba_error_buffer_v1 *error, Operation &&operation) noexcept -> neo_jieba_status_v1 {
    if (error != nullptr) {
        error->message_size = 0;
        if (error->capacity > std::numeric_limits<std::size_t>::max()
            || (error->capacity > 0 && error->data == nullptr)) {
            return NEO_JIEBA_V1_INVALID_ARGUMENT;
        }
    }
    report_error(error, NEO_JIEBA_V1_OK, {});
    try {
        return operation();
    } catch (const std::bad_alloc &) {
        return report_error(error, NEO_JIEBA_V1_OUT_OF_MEMORY, "Memory allocation failed");
    } catch (const std::length_error &failure) {
        return report_error(error, NEO_JIEBA_V1_LIMIT_EXCEEDED, failure.what());
    } catch (const neo_cppjieba::LogConfig::Exception &failure) {
        return report_error(error, NEO_JIEBA_V1_OPERATION_FAILED, failure.what());
    } catch (...) {
        std::terminate();
    }
}

// Map frozen ABI numbers explicitly so internal enum changes cannot silently change the C ABI.
auto try_cut_mode(neo_jieba_mode_v1 mode, neo_cppjieba::CutMode &out) noexcept -> bool {
    switch (mode) {
        case NEO_JIEBA_V1_MIX:
            out = neo_cppjieba::CutMode::MIX;
            return true;
        case NEO_JIEBA_V1_MIX_NO_HMM:
            out = neo_cppjieba::CutMode::MIX_NO_HMM;
            return true;
        case NEO_JIEBA_V1_FULL:
            out = neo_cppjieba::CutMode::FULL;
            return true;
        case NEO_JIEBA_V1_SEARCH:
            out = neo_cppjieba::CutMode::SEARCH;
            return true;
        case NEO_JIEBA_V1_SEARCH_NO_HMM:
            out = neo_cppjieba::CutMode::SEARCH_NO_HMM;
            return true;
        case NEO_JIEBA_V1_HMM:
            out = neo_cppjieba::CutMode::HMM;
            return true;
        case NEO_JIEBA_V1_MP:
            out = neo_cppjieba::CutMode::MP;
            return true;
        default:
            return false;
    }
}

} // namespace

// Report the byte bound before callers commit large buffers to a foreign-function call.
uint64_t neo_jieba_v1_max_input_bytes(void) {
    return max_input_bytes;
}

// Publish the engine only after dictionary and model loading both succeed.
neo_jieba_status_v1 neo_jieba_v1_engine_create(const neo_jieba_paths_v1 *paths, neo_jieba_engine_v1 **out_owned_engine,
                                               neo_jieba_error_buffer_v1 *error) {
    if (out_owned_engine != nullptr) {
        *out_owned_engine = nullptr;
    }
    return invoke_boundary(error, [&]() -> neo_jieba_status_v1 {
        if (out_owned_engine == nullptr || paths == nullptr || paths->dict_path == nullptr
            || paths->model_path == nullptr || paths->user_dict_path == nullptr || paths->dict_path[0] == '\0'
            || paths->model_path[0] == '\0') {
            return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT,
                                "Provide output storage and explicit dictionary/model paths");
        }
        auto engine = std::make_unique<neo_jieba_engine_v1>(*paths);
        *out_owned_engine = engine.release();
        return NEO_JIEBA_V1_OK;
    });
}

// Reconstruct the C++ owner only inside the library that allocated it.
void neo_jieba_v1_engine_destroy(neo_jieba_engine_v1 *owned_engine) {
    const auto owner = std::unique_ptr<neo_jieba_engine_v1>{owned_engine};
}

// Reuse the facade's decoding and segmentation; only the final representation is adapted.
neo_jieba_status_v1 neo_jieba_v1_cut_utf8_owned(const neo_jieba_engine_v1 *engine, const char *input,
                                                uint64_t input_size, neo_jieba_mode_v1 mode,
                                                neo_jieba_tokens_v1 **out_owned_tokens,
                                                neo_jieba_error_buffer_v1 *error) {
    if (out_owned_tokens != nullptr) {
        *out_owned_tokens = nullptr;
    }
    return invoke_boundary(error, [&]() -> neo_jieba_status_v1 {
        if (engine == nullptr || out_owned_tokens == nullptr || (input == nullptr && input_size != 0)) {
            return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT,
                                "Provide an engine, output storage and a valid input buffer");
        }
        auto cut_mode = neo_cppjieba::CutMode::MIX;
        if (!try_cut_mode(mode, cut_mode)) {
            return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "Unknown segmentation mode");
        }
        if (input_size > max_input_bytes) {
            return report_error(error, NEO_JIEBA_V1_LIMIT_EXCEEDED, "Input exceeds this build's UTF-8 byte limit");
        }
        auto result = std::make_unique<neo_jieba_tokens_v1>();
        if (input_size != 0) {
            result->text.assign(input, static_cast<std::size_t>(input_size));
        }
        auto workspace = neo_cppjieba::Workspace{};
        engine->jieba.cut_each(
            result->text, cut_mode,
            [&](neo_cppjieba::TokenView<char> token) {
                result->ranges.push_back({static_cast<uint64_t>(token.position.source.begin),
                                          static_cast<uint64_t>(token.position.source.size())});
            },
            workspace);
        *out_owned_tokens = result.release();
        return NEO_JIEBA_V1_OK;
    });
}

// Borrowing exposes only stable, immutable storage owned by this result.
neo_jieba_status_v1 neo_jieba_v1_tokens_borrow_data(const neo_jieba_tokens_v1 *tokens,
                                                    neo_jieba_borrowed_tokens_v1 *out_borrowed,
                                                    neo_jieba_error_buffer_v1 *error) {
    if (out_borrowed != nullptr) {
        *out_borrowed = {};
    }
    return invoke_boundary(error, [&]() -> neo_jieba_status_v1 {
        if (tokens == nullptr || out_borrowed == nullptr) {
            return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT,
                                "Provide a result and borrowed descriptor storage");
        }
        *out_borrowed = {tokens->text.empty() ? nullptr : tokens->text.data(), tokens->text.size(),
                         tokens->ranges.empty() ? nullptr : tokens->ranges.data(), tokens->ranges.size()};
        return NEO_JIEBA_V1_OK;
    });
}

// The text and range array are released together through their original allocator.
void neo_jieba_v1_tokens_destroy(neo_jieba_tokens_v1 *owned_tokens) {
    const auto owner = std::unique_ptr<neo_jieba_tokens_v1>{owned_tokens};
}
