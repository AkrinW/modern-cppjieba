#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// These values and layouts mirror the benchmark-only Rust adapter.
enum class RustCutMethod : uint32_t { Mix, Mp, Full, Search };
enum class RustFfiOutput { Borrowed, Copied };
enum class RustNativeOutput { Borrowed, Owned };

extern "C" {
struct RustJiebaHandle;

// Non-owning UTF-8 input, kept alive by the C++ caller.
struct RustInput {
    const char *data;
    size_t len;
};

// A result word either borrows the input or owns a Rust allocation.
struct FfiStr {
    const char *data;
    size_t len;
    bool owned;
};

// Rust owns the descriptor array and frees it through rust_bench_words_free.
struct CJiebaWords {
    FfiStr *words;
    size_t len;
};

// Native timing includes token destruction, with a count for validation.
struct RustMeasurement {
    double milliseconds;
    size_t tokens;
};

auto rust_bench_new(RustInput dict, RustInput model) -> RustJiebaHandle *;
auto rust_bench_free(RustJiebaHandle *jieba) -> void;
auto rust_bench_cut(const RustJiebaHandle *jieba, RustInput input, RustCutMethod method, bool copy_words)
    -> CJiebaWords *;
auto rust_bench_words_free(CJiebaWords *words) -> void;
auto rust_bench_measure(const RustJiebaHandle *jieba, const RustInput *lines, size_t len, RustCutMethod method,
                        bool owned, size_t rounds) -> RustMeasurement;
} // extern "C"

// Owns a Rust engine and exposes separate FFI and native benchmark paths.
class RustJieba {
public:
    RustJieba(std::string_view dict_path, std::string_view model_path)
        : handle_(rust_bench_new({dict_path.data(), dict_path.size()}, {model_path.data(), model_path.size()}),
                  rust_bench_free) {
        if (!handle_) {
            throw std::runtime_error("failed to load Rust dictionary or HMM model; see the preceding error");
        }
    }

    [[nodiscard]] auto cut(std::string_view sentence, RustCutMethod method, RustFfiOutput output) const
        -> std::vector<std::string> {
        const auto result = std::unique_ptr<CJiebaWords, decltype(&rust_bench_words_free)>{
            rust_bench_cut(handle_.get(), {sentence.data(), sentence.size()}, method, output == RustFfiOutput::Copied),
            rust_bench_words_free};
        if (!result) {
            throw std::runtime_error("Rust segmentation failed: input must be valid UTF-8");
        }
        auto words = std::vector<std::string>{};
        words.reserve(result->len);
        for (auto i = size_t{0}; i < result->len; ++i) {
            words.emplace_back(result->words[i].data, result->words[i].len);
        }
        return words;
    }

    [[nodiscard]] auto benchmark(const std::vector<std::string> &lines, RustCutMethod method, RustNativeOutput output,
                                 size_t rounds) const -> RustMeasurement {
        auto inputs = std::vector<RustInput>{};
        inputs.reserve(lines.size());
        for (const auto &line : lines) {
            inputs.push_back({line.data(), line.size()});
        }
        const auto result = rust_bench_measure(handle_.get(), inputs.data(), inputs.size(), method,
                                               output == RustNativeOutput::Owned, rounds);
        if (result.milliseconds < 0.0) {
            throw std::runtime_error("Rust native benchmark failed: input must be valid UTF-8");
        }
        return result;
    }

private:
    std::unique_ptr<RustJiebaHandle, decltype(&rust_bench_free)> handle_;
};
