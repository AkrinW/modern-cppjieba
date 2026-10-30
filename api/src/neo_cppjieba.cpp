#include "neo_cppjieba.h"

#include "neo/Config.hpp"
#include "neo/Jieba.hpp"
#include "neo/KeywordExtractor.hpp"
#include "neo/util/Logging.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The opaque engine owns the existing facade without introducing shared scratch buffers.
struct neo_jieba_engine_v1 {
    explicit neo_jieba_engine_v1(neo_cppjieba::Jieba loaded) noexcept : jieba(std::move(loaded)) {
    }

    neo_cppjieba::Jieba jieba;
};

// No stored pointers refer to the engine, caller input, or a temporary decoding workspace.
struct neo_jieba_tokens_v1 {
    std::string text;
    std::vector<neo_jieba_token_range_v1> ranges;
};

// Reusable scratch storage; results never point into it, so destroying it leaves filled ranges valid.
struct neo_jieba_workspace_v1 {
    neo_cppjieba::Workspace segment;
    neo_cppjieba::KeywordWorkspace keyword;
};

// Byte ranges into the input of the latest fill; no input text is copied or retained.
// Tags are empty after an untagged fill and otherwise parallel to values.
struct neo_jieba_ranges_v1 {
    std::vector<neo_jieba_token_range_v1> values;
    std::vector<neo_jieba_pos_tag_v1> tags;

    auto clear() noexcept -> void {
        values.clear();
        tags.clear();
    }
};

// Keyword resources only; each extraction takes its engine explicitly and reads this extractor.
struct neo_jieba_tfidf_v1 {
    explicit neo_jieba_tfidf_v1(neo_cppjieba::TfIdfExtractor loaded) noexcept : extractor(std::move(loaded)) {
    }

    neo_cppjieba::TfIdfExtractor extractor;
};

// Keyword resources only; each extraction takes its engine explicitly and reads this extractor.
struct neo_jieba_textrank_v1 {
    explicit neo_jieba_textrank_v1(neo_cppjieba::TextRankExtractor loaded) noexcept : extractor(std::move(loaded)) {
    }

    neo_cppjieba::TextRankExtractor extractor;
};

// Keywords of the latest fill as C records over one occurrence array; found keeps the C++ results' capacity.
struct neo_jieba_keywords_v1 {
    std::vector<neo_jieba_keyword_v1> values;
    std::vector<neo_jieba_token_range_v1> occurrences;
    std::vector<neo_cppjieba::Keyword<char>> found;

    auto clear() noexcept -> void {
        values.clear();
        occurrences.clear();
    }
};

namespace {

constexpr auto max_input_bytes =
    std::min({static_cast<uint64_t>(std::numeric_limits<neo_cppjieba::SourceOffset>::max()),
              static_cast<uint64_t>(std::numeric_limits<neo_cppjieba::RuneIndex>::max()),
              static_cast<uint64_t>(std::numeric_limits<std::size_t>::max() - 1),
              static_cast<uint64_t>(std::numeric_limits<std::ptrdiff_t>::max() - 1)});

static_assert(sizeof(neo_jieba_token_range_v1) == 2 * sizeof(uint64_t));
static_assert(offsetof(neo_jieba_token_range_v1, byte_length) == sizeof(uint64_t));
static_assert(sizeof(neo_jieba_pos_tag_v1) == neo_cppjieba::PosTag::MaxLength);

// ── Boundary ─────────────────────────────────────────────────────────────
//
// Every entry point creates an owner, fills a reusable result, borrows a result's storage, or only reports a
// status. The helpers below give each shape one failure rule, so entry points only check arguments and call C++.

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

// Publish the owner make() builds once check() passes; the output slot stays NULL on every failure.
template <typename Owner, typename Check, typename Make>
auto publish_owner(Owner **out_owned, neo_jieba_error_buffer_v1 *error, const Check &check, const Make &make) noexcept
    -> neo_jieba_status_v1 {
    if (out_owned != nullptr) {
        *out_owned = nullptr;
    }
    return invoke_boundary(error, [&]() -> neo_jieba_status_v1 {
        if (out_owned == nullptr) {
            return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "Provide output storage");
        }
        if (const auto checked = check(); checked != NEO_JIEBA_V1_OK) {
            return checked;
        }
        *out_owned = make().release();
        return NEO_JIEBA_V1_OK;
    });
}

// Reusable handles start empty and take no arguments beyond their output slot.
template <typename Owner>
auto publish_empty(Owner **out_owned, neo_jieba_error_buffer_v1 *error) noexcept -> neo_jieba_status_v1 {
    return publish_owner(
        out_owned, error, [] { return neo_jieba_status_v1{NEO_JIEBA_V1_OK}; },
        [] { return std::make_unique<Owner>(); });
}

// Replace a reusable result: it is empty before fill() runs and after any failure, so no partial fill is published.
template <typename Result, typename Fill>
auto fill_result(Result *result, neo_jieba_error_buffer_v1 *error, const Fill &fill) noexcept -> neo_jieba_status_v1 {
    if (result != nullptr) {
        result->clear();
    }
    const auto status = invoke_boundary(error, fill);
    if (status != NEO_JIEBA_V1_OK && result != nullptr) {
        result->clear();
    }
    return status;
}

// Describe a result's storage without transferring ownership; a valid output is zeroed on failure.
template <typename Result, typename Borrowed, typename Describe>
auto borrow_result(const Result *result, Borrowed *out_borrowed, neo_jieba_error_buffer_v1 *error,
                   const Describe &describe) noexcept -> neo_jieba_status_v1 {
    if (out_borrowed != nullptr) {
        *out_borrowed = {};
    }
    return invoke_boundary(error, [&]() -> neo_jieba_status_v1 {
        if (result == nullptr || out_borrowed == nullptr) {
            return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT,
                                "Provide a result and borrowed descriptor storage");
        }
        *out_borrowed = describe(*result);
        return NEO_JIEBA_V1_OK;
    });
}

// Empty borrowed arrays are NULL, as the header documents.
template <typename Container>
auto data_or_null(const Container &values) noexcept -> const typename Container::value_type * {
    return values.empty() ? nullptr : values.data();
}

// ── Argument checks ──────────────────────────────────────────────────────

// NULL is a valid caller buffer exactly when it is empty.
auto is_valid_buffer(const char *data, uint64_t size) noexcept -> bool {
    return data != nullptr || size == 0;
}

// View caller bytes whose size has already been checked against an addressable limit.
auto as_text(const char *data, uint64_t size) noexcept -> std::string_view {
    return {data, static_cast<std::size_t>(size)};
}

// Every text entry point bounds its input before reading any byte.
auto check_input_size(uint64_t size, neo_jieba_error_buffer_v1 *error) noexcept -> neo_jieba_status_v1 {
    if (size > max_input_bytes) {
        return report_error(error, NEO_JIEBA_V1_LIMIT_EXCEEDED, "Input exceeds this build's UTF-8 byte limit");
    }
    return NEO_JIEBA_V1_OK;
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

// Map the mode and bound the input size before any input byte is read.
auto check_cut_request(neo_jieba_mode_v1 mode, uint64_t input_size, neo_cppjieba::CutMode &out,
                       neo_jieba_error_buffer_v1 *error) noexcept -> neo_jieba_status_v1 {
    if (!try_cut_mode(mode, out)) {
        return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "Unknown segmentation mode");
    }
    return check_input_size(input_size, error);
}

// Only canonical NUL padding is accepted, so a stored tag reads back byte for byte.
auto is_padded_tag(const neo_jieba_pos_tag_v1 &tag) noexcept -> bool {
    return std::all_of(std::ranges::find(tag.data, '\0'), std::ranges::end(tag.data),
                       [](char byte) { return byte == '\0'; });
}

// Edits need a nonempty word within the input limit; decoding later rejects invalid UTF-8.
auto check_edit_word(const neo_jieba_engine_v1 *engine, const char *word, uint64_t word_size,
                     neo_jieba_error_buffer_v1 *error) noexcept -> neo_jieba_status_v1 {
    if (engine == nullptr || word == nullptr || word_size == 0) {
        return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "Provide an engine and a nonempty word");
    }
    return check_input_size(word_size, error);
}

// Additions also need a tag whose padding reads back unchanged.
auto check_word_addition(const neo_jieba_engine_v1 *engine, const char *word, uint64_t word_size,
                         const neo_jieba_pos_tag_v1 *tag, neo_jieba_error_buffer_v1 *error) noexcept
    -> neo_jieba_status_v1 {
    if (const auto checked = check_edit_word(engine, word, word_size, error); checked != NEO_JIEBA_V1_OK) {
        return checked;
    }
    if (tag == nullptr || !is_padded_tag(*tag)) {
        return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "Provide a NUL-padded part-of-speech tag");
    }
    return NEO_JIEBA_V1_OK;
}

// Paths reach the file layer whole, so an embedded NUL would silently name another file.
auto check_path(const char *path, uint64_t path_size, neo_jieba_error_buffer_v1 *error) noexcept
    -> neo_jieba_status_v1 {
    if (path == nullptr || path_size == 0) {
        return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "Provide a nonempty file path");
    }
    if (path_size > std::numeric_limits<std::size_t>::max()) {
        return report_error(error, NEO_JIEBA_V1_LIMIT_EXCEEDED, "File path exceeds this build's addressable size");
    }
    if (as_text(path, path_size).find('\0') != std::string_view::npos) {
        return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "File paths must not contain NUL bytes");
    }
    return NEO_JIEBA_V1_OK;
}

// Memory contents are borrowed whole, so their size must be addressable before a view is formed.
auto check_memory(const char *data, uint64_t size, neo_jieba_error_buffer_v1 *error) noexcept -> neo_jieba_status_v1 {
    if (!is_valid_buffer(data, size)) {
        return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "Provide a valid memory source buffer");
    }
    if (size > std::numeric_limits<std::size_t>::max()) {
        return report_error(error, NEO_JIEBA_V1_LIMIT_EXCEEDED, "Memory source exceeds this build's addressable size");
    }
    return NEO_JIEBA_V1_OK;
}

// Map frozen source kinds explicitly; reserved fields stay zero so later releases can give them meaning.
auto check_source(const neo_jieba_source_v1 *source, neo_jieba_error_buffer_v1 *error) noexcept -> neo_jieba_status_v1 {
    if (source == nullptr || source->reserved != 0) {
        return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "Provide each source with a zero reserved field");
    }
    switch (source->kind) {
        case NEO_JIEBA_V1_SOURCE_FILE:
            return check_path(source->data, source->size, error);
        case NEO_JIEBA_V1_SOURCE_MEMORY:
            return check_memory(source->data, source->size, error);
        default:
            return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "Unknown source kind");
    }
}

// Check single sources in argument order and report the first failure.
auto check_sources(std::initializer_list<const neo_jieba_source_v1 *> sources,
                   neo_jieba_error_buffer_v1 *error) noexcept -> neo_jieba_status_v1 {
    for (const auto *source : sources) {
        if (const auto checked = check_source(source, error); checked != NEO_JIEBA_V1_OK) {
            return checked;
        }
    }
    return NEO_JIEBA_V1_OK;
}

// Check every user dictionary before any loader runs, so argument errors never follow partial work.
auto check_user_dicts(const neo_jieba_source_v1 *user_dicts, uint64_t count, neo_jieba_error_buffer_v1 *error) noexcept
    -> neo_jieba_status_v1 {
    if (user_dicts == nullptr && count != 0) {
        return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT,
                            "Provide user dictionary storage for a nonzero count");
    }
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(neo_jieba_source_v1)) {
        return report_error(error, NEO_JIEBA_V1_LIMIT_EXCEEDED, "User dictionary count exceeds this build's limit");
    }
    for (const auto &source : std::span{user_dicts, static_cast<std::size_t>(count)}) {
        if (const auto checked = check_source(&source, error); checked != NEO_JIEBA_V1_OK) {
            return checked;
        }
    }
    return NEO_JIEBA_V1_OK;
}

// The main and user dictionaries that engine loading and snapshot writing both read.
auto check_dictionaries(const neo_jieba_source_v1 *dict, const neo_jieba_source_v1 *user_dicts, uint64_t count,
                        neo_jieba_error_buffer_v1 *error) noexcept -> neo_jieba_status_v1 {
    if (const auto checked = check_source(dict, error); checked != NEO_JIEBA_V1_OK) {
        return checked;
    }
    return check_user_dicts(user_dicts, count, error);
}

// Allowed tags must be padded, so filtering compares exactly the bytes a tagged fill reports.
auto check_allowed_tags(const neo_jieba_pos_tag_v1 *tags, uint64_t count, neo_jieba_error_buffer_v1 *error) noexcept
    -> neo_jieba_status_v1 {
    if (tags == nullptr && count != 0) {
        return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "Provide allowed tag storage for a nonzero count");
    }
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(neo_jieba_pos_tag_v1)) {
        return report_error(error, NEO_JIEBA_V1_LIMIT_EXCEEDED, "Allowed tag count exceeds this build's limit");
    }
    if (!std::ranges::all_of(std::span{tags, static_cast<std::size_t>(count)}, is_padded_tag)) {
        return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "Allowed tags must be NUL-padded");
    }
    return NEO_JIEBA_V1_OK;
}

// Options are checked before extraction, so invalid configuration is an argument error, not a failed operation.
auto check_options(const neo_jieba_tfidf_options_v1 *options, neo_jieba_error_buffer_v1 *error) noexcept
    -> neo_jieba_status_v1 {
    if (options == nullptr) {
        return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "Provide keyword options");
    }
    return check_allowed_tags(options->allowed_tags, options->allowed_tag_count, error);
}

// NaN damping fails both comparisons and is rejected with other out-of-range values.
auto check_options(const neo_jieba_textrank_options_v1 *options, neo_jieba_error_buffer_v1 *error) noexcept
    -> neo_jieba_status_v1 {
    if (options == nullptr) {
        return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "Provide keyword options");
    }
    if (options->window > std::numeric_limits<std::size_t>::max()
        || options->iterations > std::numeric_limits<std::size_t>::max()) {
        return report_error(error, NEO_JIEBA_V1_LIMIT_EXCEEDED,
                            "TextRank window and iterations exceed this build's size type");
    }
    const auto damping_in_range = options->damping >= 0.0 && options->damping <= 1.0;
    if (!damping_in_range) {
        return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "TextRank damping must lie in [0, 1]");
    }
    return check_allowed_tags(options->allowed_tags, options->allowed_tag_count, error);
}

// ── Conversions ──────────────────────────────────────────────────────────

// Source offsets of UTF-8 input are byte offsets.
auto to_c_range(const neo_cppjieba::TokenPosition &position) noexcept -> neo_jieba_token_range_v1 {
    return {static_cast<uint64_t>(position.source.begin), static_cast<uint64_t>(position.source.size())};
}

// Copy all four bytes, keeping the NUL padding of shorter tags.
auto to_c_tag(const neo_cppjieba::PosTag &tag) noexcept -> neo_jieba_pos_tag_v1 {
    auto result = neo_jieba_pos_tag_v1{};
    std::copy_n(tag.data(), neo_cppjieba::PosTag::MaxLength, result.data);
    return result;
}

// The text of a padded tag ends at its first NUL, or after all four bytes.
auto to_cpp_tag(const neo_jieba_pos_tag_v1 &tag) -> neo_cppjieba::PosTag {
    return neo_cppjieba::PosTag{std::string_view{std::ranges::begin(tag.data), std::ranges::find(tag.data, '\0')}};
}

// Convert checked allowed tags; an empty array keeps every candidate.
auto to_cpp_tags(const neo_jieba_pos_tag_v1 *tags, uint64_t count) -> std::vector<neo_cppjieba::PosTag> {
    const auto source = std::span{tags, static_cast<std::size_t>(count)};
    auto result = std::vector<neo_cppjieba::PosTag>{};
    result.reserve(source.size());
    std::ranges::transform(source, std::back_inserter(result), to_cpp_tag);
    return result;
}

// Convert checked TF-IDF options.
auto to_cpp_options(const neo_jieba_tfidf_options_v1 &options) -> neo_cppjieba::TfIdfOptions {
    return {to_cpp_tags(options.allowed_tags, options.allowed_tag_count)};
}

// Convert checked TextRank options; window and iterations fit size_t after checking.
auto to_cpp_options(const neo_jieba_textrank_options_v1 &options) -> neo_cppjieba::TextRankOptions {
    return {static_cast<std::size_t>(options.window), static_cast<std::size_t>(options.iterations), options.damping,
            to_cpp_tags(options.allowed_tags, options.allowed_tag_count)};
}

// Memory sources carry their role as the diagnostic name; file sources are named by their path.
constexpr auto dict_memory_name = std::string_view{"<memory:dict>"};
constexpr auto model_memory_name = std::string_view{"<memory:model>"};
constexpr auto snapshot_memory_name = std::string_view{"<memory:dict_snapshot>"};
constexpr auto idf_memory_name = std::string_view{"<memory:idf>"};
constexpr auto stop_words_memory_name = std::string_view{"<memory:stop_words>"};

// Convert a checked source; the loader copies file paths and borrows memory contents.
auto to_dict_source(const neo_jieba_source_v1 &source, std::string_view memory_name) -> neo_cppjieba::DictSource {
    const auto bytes = as_text(source.data, source.size);
    if (source.kind == NEO_JIEBA_V1_SOURCE_FILE) {
        return neo_cppjieba::DictSource::file(bytes);
    }
    return neo_cppjieba::DictSource::memory(bytes, memory_name);
}

// Number user dictionaries in diagnostics by their position in the caller's array.
auto to_user_dict_sources(const neo_jieba_source_v1 *user_dicts, uint64_t count)
    -> std::vector<neo_cppjieba::DictSource> {
    const auto sources = std::span{user_dicts, static_cast<std::size_t>(count)};
    auto result = std::vector<neo_cppjieba::DictSource>{};
    result.reserve(sources.size());
    for (auto index = std::size_t{0}; index < sources.size(); ++index) {
        result.push_back(to_dict_source(sources[index], std::format("<memory:user_dict[{}]>", index)));
    }
    return result;
}

// Convert a checked snapshot source; memory bytes are borrowed until loading returns.
auto to_snapshot_source(const neo_jieba_source_v1 &source) -> neo_cppjieba::SnapshotSource {
    const auto bytes = as_text(source.data, source.size);
    if (source.kind == NEO_JIEBA_V1_SOURCE_FILE) {
        return neo_cppjieba::SnapshotSource::file(bytes);
    }
    return neo_cppjieba::SnapshotSource::memory(std::as_bytes(std::span{bytes.data(), bytes.size()}),
                                                snapshot_memory_name);
}

// ── Operations ───────────────────────────────────────────────────────────

// Append each token's byte range.
auto append_ranges(const neo_cppjieba::Jieba &jieba, std::string_view text, neo_cppjieba::CutMode mode,
                   neo_cppjieba::Workspace &workspace, std::vector<neo_jieba_token_range_v1> &out) -> void {
    jieba.cut_each(
        text, mode, [&out](neo_cppjieba::TokenView<char> token) { out.push_back(to_c_range(token.position)); },
        workspace);
}

// Append each token's byte range and its tag as parallel arrays.
auto append_tagged_ranges(const neo_cppjieba::Jieba &jieba, std::string_view text, neo_cppjieba::CutMode mode,
                          neo_cppjieba::Workspace &workspace, neo_jieba_ranges_v1 &out) -> void {
    jieba.cut_each<neo_cppjieba::Tagging::Pos>(
        text, mode,
        [&out](const neo_cppjieba::TaggedTokenView<char> &token) {
            out.values.push_back(to_c_range(token.position));
            out.tags.push_back(to_c_tag(token.tag));
        },
        workspace);
}

// Segment caller input through the caller's workspace; Tagging::Pos also fills the parallel tags.
template <neo_cppjieba::Tagging tagging>
auto fill_ranges(const neo_jieba_engine_v1 *engine, const char *input, uint64_t input_size, neo_jieba_mode_v1 mode,
                 neo_jieba_workspace_v1 *workspace, neo_jieba_ranges_v1 *ranges,
                 neo_jieba_error_buffer_v1 *error) noexcept -> neo_jieba_status_v1 {
    return fill_result(ranges, error, [&]() -> neo_jieba_status_v1 {
        if (engine == nullptr || workspace == nullptr || ranges == nullptr || !is_valid_buffer(input, input_size)) {
            return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT,
                                "Provide an engine, a workspace, a ranges result and a valid input buffer");
        }
        auto cut_mode = neo_cppjieba::CutMode::MIX;
        if (const auto checked = check_cut_request(mode, input_size, cut_mode, error); checked != NEO_JIEBA_V1_OK) {
            return checked;
        }
        const auto text = as_text(input, input_size);
        if constexpr (tagging == neo_cppjieba::Tagging::Pos) {
            append_tagged_ranges(engine->jieba, text, cut_mode, workspace->segment, *ranges);
        } else {
            append_ranges(engine->jieba, text, cut_mode, workspace->segment, ranges->values);
        }
        return NEO_JIEBA_V1_OK;
    });
}

// Flatten keywords into C records whose occurrences are contiguous slices of one range array.
auto publish_keywords(const std::vector<neo_cppjieba::Keyword<char>> &found, neo_jieba_keywords_v1 &out) -> void {
    out.values.reserve(found.size());
    for (const auto &keyword : found) {
        neo_cppjieba::assert_check([&] { return !keyword.positions.empty(); },
                                   "Every keyword occurs in the input it came from");
        out.values.push_back(
            {keyword.weight, out.occurrences.size(), keyword.positions.size(), to_c_tag(keyword.tag), 0});
        std::ranges::transform(keyword.positions, std::back_inserter(out.occurrences), to_c_range);
    }
}

// Shared by both rankers; only the extractor and option types differ.
template <typename Extractor, typename Options>
auto extract_keywords(const Extractor *extractor, const neo_jieba_engine_v1 *engine, const char *input,
                      uint64_t input_size, uint64_t top_n, const Options *options, neo_jieba_workspace_v1 *workspace,
                      neo_jieba_keywords_v1 *keywords, neo_jieba_error_buffer_v1 *error) noexcept
    -> neo_jieba_status_v1 {
    return fill_result(keywords, error, [&]() -> neo_jieba_status_v1 {
        if (extractor == nullptr || engine == nullptr || workspace == nullptr || keywords == nullptr
            || !is_valid_buffer(input, input_size)) {
            return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT,
                                "Provide an extractor, an engine, a workspace, a keywords result and a valid input");
        }
        if (const auto checked = check_input_size(input_size, error); checked != NEO_JIEBA_V1_OK) {
            return checked;
        }
        if (const auto checked = check_options(options, error); checked != NEO_JIEBA_V1_OK) {
            return checked;
        }
        // No result can hold more keywords than size_t counts, so clamping keeps the request's meaning.
        const auto limit =
            static_cast<std::size_t>(std::min(top_n, static_cast<uint64_t>(std::numeric_limits<std::size_t>::max())));
        extractor->extractor.extract_into(engine->jieba, as_text(input, input_size), limit, to_cpp_options(*options),
                                          keywords->found, workspace->keyword);
        publish_keywords(keywords->found, *keywords);
        return NEO_JIEBA_V1_OK;
    });
}

} // namespace

// Report the byte bound before callers commit large buffers to a foreign-function call.
uint64_t neo_jieba_v1_max_input_bytes(void) {
    return max_input_bytes;
}

// Publish the engine only after dictionary and model loading both succeed.
neo_jieba_status_v1 neo_jieba_v1_engine_create(const neo_jieba_paths_v1 *paths, neo_jieba_engine_v1 **out_owned_engine,
                                               neo_jieba_error_buffer_v1 *error) {
    return publish_owner(
        out_owned_engine, error,
        [&]() -> neo_jieba_status_v1 {
            const auto explicit_paths = paths != nullptr && paths->dict_path != nullptr && paths->model_path != nullptr
                                        && paths->user_dict_path != nullptr && paths->dict_path[0] != '\0'
                                        && paths->model_path[0] != '\0';
            if (!explicit_paths) {
                return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "Provide explicit dictionary/model paths");
            }
            return NEO_JIEBA_V1_OK;
        },
        [&] {
            return std::make_unique<neo_jieba_engine_v1>(
                neo_cppjieba::Jieba{paths->dict_path, paths->model_path, paths->user_dict_path});
        });
}

// Publish the engine only after every source is checked and all of them load.
neo_jieba_status_v1
neo_jieba_v1_engine_create_from_sources(const neo_jieba_source_v1 *dict, const neo_jieba_source_v1 *model,
                                        const neo_jieba_source_v1 *user_dicts, uint64_t user_dict_count,
                                        neo_jieba_engine_v1 **out_owned_engine, neo_jieba_error_buffer_v1 *error) {
    return publish_owner(
        out_owned_engine, error,
        [&] {
            const auto checked = check_dictionaries(dict, user_dicts, user_dict_count, error);
            return checked != NEO_JIEBA_V1_OK ? checked : check_source(model, error);
        },
        [&] {
            return std::make_unique<neo_jieba_engine_v1>(
                neo_cppjieba::Jieba{to_dict_source(*dict, dict_memory_name), to_dict_source(*model, model_memory_name),
                                    to_user_dict_sources(user_dicts, user_dict_count)});
        });
}

// Snapshot loading skips dictionary parsing; the model still loads from its source.
neo_jieba_status_v1 neo_jieba_v1_engine_create_from_snapshot(const neo_jieba_source_v1 *dict_snapshot,
                                                             const neo_jieba_source_v1 *model,
                                                             neo_jieba_engine_v1 **out_owned_engine,
                                                             neo_jieba_error_buffer_v1 *error) {
    return publish_owner(
        out_owned_engine, error, [&] { return check_sources({dict_snapshot, model}, error); },
        [&] {
            return std::make_unique<neo_jieba_engine_v1>(neo_cppjieba::Jieba::from_snapshot(
                to_snapshot_source(*dict_snapshot), to_dict_source(*model, model_memory_name)));
        });
}

// The C++ writer publishes atomically, so a failure leaves any previous file in place.
neo_jieba_status_v1 neo_jieba_v1_write_dict_snapshot(const neo_jieba_source_v1 *dict,
                                                     const neo_jieba_source_v1 *user_dicts, uint64_t user_dict_count,
                                                     const char *path, uint64_t path_size,
                                                     neo_jieba_error_buffer_v1 *error) {
    return invoke_boundary(error, [&]() -> neo_jieba_status_v1 {
        if (const auto checked = check_dictionaries(dict, user_dicts, user_dict_count, error);
            checked != NEO_JIEBA_V1_OK) {
            return checked;
        }
        if (const auto checked = check_path(path, path_size, error); checked != NEO_JIEBA_V1_OK) {
            return checked;
        }
        neo_cppjieba::Jieba::write_dict_snapshot(to_dict_source(*dict, dict_memory_name),
                                                 to_user_dict_sources(user_dicts, user_dict_count),
                                                 as_text(path, path_size));
        return NEO_JIEBA_V1_OK;
    });
}

// The copy shares no storage with the original, so either can be edited or destroyed independently.
neo_jieba_status_v1 neo_jieba_v1_engine_clone(const neo_jieba_engine_v1 *engine, neo_jieba_engine_v1 **out_owned_engine,
                                              neo_jieba_error_buffer_v1 *error) {
    return publish_owner(
        out_owned_engine, error,
        [&]() -> neo_jieba_status_v1 {
            if (engine == nullptr) {
                return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "Provide an engine");
            }
            return NEO_JIEBA_V1_OK;
        },
        [&] { return std::make_unique<neo_jieba_engine_v1>(engine->jieba.clone()); });
}

// Reconstruct the C++ owner only inside the library that allocated it.
void neo_jieba_v1_engine_destroy(neo_jieba_engine_v1 *owned_engine) {
    const auto owner = std::unique_ptr<neo_jieba_engine_v1>{owned_engine};
}

// Runtime words behave like user dictionary entries; the library takes no lock around the edit.
neo_jieba_status_v1 neo_jieba_v1_engine_add_word(neo_jieba_engine_v1 *engine, const char *word, uint64_t word_size,
                                                 const neo_jieba_pos_tag_v1 *tag, neo_jieba_error_buffer_v1 *error) {
    return invoke_boundary(error, [&]() -> neo_jieba_status_v1 {
        if (const auto checked = check_word_addition(engine, word, word_size, tag, error); checked != NEO_JIEBA_V1_OK) {
            return checked;
        }
        engine->jieba.add_word(as_text(word, word_size), to_cpp_tag(*tag));
        return NEO_JIEBA_V1_OK;
    });
}

// A non-positive frequency is an argument error here, before the dictionary's own check.
neo_jieba_status_v1 neo_jieba_v1_engine_add_word_with_freq(neo_jieba_engine_v1 *engine, const char *word,
                                                           uint64_t word_size, int32_t freq,
                                                           const neo_jieba_pos_tag_v1 *tag,
                                                           neo_jieba_error_buffer_v1 *error) {
    return invoke_boundary(error, [&]() -> neo_jieba_status_v1 {
        if (const auto checked = check_word_addition(engine, word, word_size, tag, error); checked != NEO_JIEBA_V1_OK) {
            return checked;
        }
        if (freq <= 0) {
            return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "Word frequency must be positive");
        }
        engine->jieba.add_word_with_freq(as_text(word, word_size), freq, to_cpp_tag(*tag));
        return NEO_JIEBA_V1_OK;
    });
}

// The removal flag is cleared first, so every failure reports that nothing was removed.
neo_jieba_status_v1 neo_jieba_v1_engine_remove_word(neo_jieba_engine_v1 *engine, const char *word, uint64_t word_size,
                                                    uint32_t *out_removed, neo_jieba_error_buffer_v1 *error) {
    if (out_removed != nullptr) {
        *out_removed = 0;
    }
    return invoke_boundary(error, [&]() -> neo_jieba_status_v1 {
        if (out_removed == nullptr) {
            return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "Provide storage for the removal flag");
        }
        if (const auto checked = check_edit_word(engine, word, word_size, error); checked != NEO_JIEBA_V1_OK) {
            return checked;
        }
        *out_removed = engine->jieba.remove_word(as_text(word, word_size)) ? 1 : 0;
        return NEO_JIEBA_V1_OK;
    });
}

// Compaction rebuilds trie storage without changing any lookup result.
neo_jieba_status_v1 neo_jieba_v1_engine_compact(neo_jieba_engine_v1 *engine, neo_jieba_error_buffer_v1 *error) {
    return invoke_boundary(error, [&]() -> neo_jieba_status_v1 {
        if (engine == nullptr) {
            return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "Provide an engine");
        }
        engine->jieba.compact();
        return NEO_JIEBA_V1_OK;
    });
}

// Reuse the facade's decoding and segmentation; only the final representation is adapted.
neo_jieba_status_v1 neo_jieba_v1_cut_utf8_owned(const neo_jieba_engine_v1 *engine, const char *input,
                                                uint64_t input_size, neo_jieba_mode_v1 mode,
                                                neo_jieba_tokens_v1 **out_owned_tokens,
                                                neo_jieba_error_buffer_v1 *error) {
    auto cut_mode = neo_cppjieba::CutMode::MIX;
    return publish_owner(
        out_owned_tokens, error,
        [&]() -> neo_jieba_status_v1 {
            if (engine == nullptr || !is_valid_buffer(input, input_size)) {
                return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT, "Provide an engine and a valid input buffer");
            }
            return check_cut_request(mode, input_size, cut_mode, error);
        },
        [&] {
            auto result = std::make_unique<neo_jieba_tokens_v1>();
            if (input_size != 0) {
                result->text.assign(input, static_cast<std::size_t>(input_size));
            }
            auto workspace = neo_cppjieba::Workspace{};
            append_ranges(engine->jieba, result->text, cut_mode, workspace, result->ranges);
            return result;
        });
}

// Borrowing exposes only stable, immutable storage owned by this result.
neo_jieba_status_v1 neo_jieba_v1_tokens_borrow_data(const neo_jieba_tokens_v1 *tokens,
                                                    neo_jieba_borrowed_tokens_v1 *out_borrowed,
                                                    neo_jieba_error_buffer_v1 *error) {
    return borrow_result(tokens, out_borrowed, error, [](const neo_jieba_tokens_v1 &result) {
        return neo_jieba_borrowed_tokens_v1{data_or_null(result.text), result.text.size(), data_or_null(result.ranges),
                                            result.ranges.size()};
    });
}

// The text and range array are released together through their original allocator.
void neo_jieba_v1_tokens_destroy(neo_jieba_tokens_v1 *owned_tokens) {
    const auto owner = std::unique_ptr<neo_jieba_tokens_v1>{owned_tokens};
}

// Scratch storage starts empty and grows on first use.
neo_jieba_status_v1 neo_jieba_v1_workspace_create(neo_jieba_workspace_v1 **out_owned_workspace,
                                                  neo_jieba_error_buffer_v1 *error) {
    return publish_empty(out_owned_workspace, error);
}

// Reconstruct the C++ owner only inside the library that allocated it.
void neo_jieba_v1_workspace_destroy(neo_jieba_workspace_v1 *owned_workspace) {
    const auto owner = std::unique_ptr<neo_jieba_workspace_v1>{owned_workspace};
}

// A ranges result starts empty; its capacity grows with the largest fill.
neo_jieba_status_v1 neo_jieba_v1_ranges_create(neo_jieba_ranges_v1 **out_owned_ranges,
                                               neo_jieba_error_buffer_v1 *error) {
    return publish_empty(out_owned_ranges, error);
}

// Reconstruct the C++ owner only inside the library that allocated it.
void neo_jieba_v1_ranges_destroy(neo_jieba_ranges_v1 *owned_ranges) {
    const auto owner = std::unique_ptr<neo_jieba_ranges_v1>{owned_ranges};
}

// Reuse caller-owned scratch and output capacity; ranges index the caller's input, which is never copied.
neo_jieba_status_v1 neo_jieba_v1_cut_utf8_into(const neo_jieba_engine_v1 *engine, const char *input,
                                               uint64_t input_size, neo_jieba_mode_v1 mode,
                                               neo_jieba_workspace_v1 *workspace, neo_jieba_ranges_v1 *ranges,
                                               neo_jieba_error_buffer_v1 *error) {
    return fill_ranges<neo_cppjieba::Tagging::None>(engine, input, input_size, mode, workspace, ranges, error);
}

// Tags are looked up after segmentation, so tagged and untagged fills produce the same ranges.
neo_jieba_status_v1 neo_jieba_v1_cut_utf8_tagged_into(const neo_jieba_engine_v1 *engine, const char *input,
                                                      uint64_t input_size, neo_jieba_mode_v1 mode,
                                                      neo_jieba_workspace_v1 *workspace, neo_jieba_ranges_v1 *ranges,
                                                      neo_jieba_error_buffer_v1 *error) {
    return fill_ranges<neo_cppjieba::Tagging::Pos>(engine, input, input_size, mode, workspace, ranges, error);
}

// Borrowing exposes the latest fill without copying; the array stays stable until the next fill or destroy.
neo_jieba_status_v1 neo_jieba_v1_ranges_borrow_data(const neo_jieba_ranges_v1 *ranges,
                                                    neo_jieba_borrowed_ranges_v1 *out_borrowed,
                                                    neo_jieba_error_buffer_v1 *error) {
    return borrow_result(ranges, out_borrowed, error, [](const neo_jieba_ranges_v1 &result) {
        neo_cppjieba::assert_check([&] { return result.tags.empty() || result.tags.size() == result.values.size(); },
                                   "Range tags must be empty or parallel to the ranges");
        return neo_jieba_borrowed_ranges_v1{data_or_null(result.values), result.values.size(),
                                            data_or_null(result.tags)};
    });
}

// A word is tagged whole, without segmentation; decoding still rejects invalid UTF-8.
neo_jieba_status_v1 neo_jieba_v1_lookup_tag(const neo_jieba_engine_v1 *engine, const char *word, uint64_t word_size,
                                            neo_jieba_pos_tag_v1 *out_tag, neo_jieba_error_buffer_v1 *error) {
    if (out_tag != nullptr) {
        *out_tag = {};
    }
    return invoke_boundary(error, [&]() -> neo_jieba_status_v1 {
        if (engine == nullptr || out_tag == nullptr || !is_valid_buffer(word, word_size)) {
            return report_error(error, NEO_JIEBA_V1_INVALID_ARGUMENT,
                                "Provide an engine, tag storage and a valid word buffer");
        }
        if (const auto checked = check_input_size(word_size, error); checked != NEO_JIEBA_V1_OK) {
            return checked;
        }
        *out_tag = to_c_tag(engine->jieba.lookup_tag(as_text(word, word_size)));
        return NEO_JIEBA_V1_OK;
    });
}

// Publish the extractor only after both sources are checked and loaded.
neo_jieba_status_v1 neo_jieba_v1_tfidf_create(const neo_jieba_source_v1 *idf, const neo_jieba_source_v1 *stop_words,
                                              neo_jieba_tfidf_v1 **out_owned_extractor,
                                              neo_jieba_error_buffer_v1 *error) {
    return publish_owner(
        out_owned_extractor, error, [&] { return check_sources({idf, stop_words}, error); },
        [&] {
            return std::make_unique<neo_jieba_tfidf_v1>(neo_cppjieba::TfIdfExtractor{
                to_dict_source(*idf, idf_memory_name), to_dict_source(*stop_words, stop_words_memory_name)});
        });
}

// Reconstruct the C++ owner only inside the library that allocated it.
void neo_jieba_v1_tfidf_destroy(neo_jieba_tfidf_v1 *owned_extractor) {
    const auto owner = std::unique_ptr<neo_jieba_tfidf_v1>{owned_extractor};
}

// Publish the extractor only after its stop words are checked and loaded.
neo_jieba_status_v1 neo_jieba_v1_textrank_create(const neo_jieba_source_v1 *stop_words,
                                                 neo_jieba_textrank_v1 **out_owned_extractor,
                                                 neo_jieba_error_buffer_v1 *error) {
    return publish_owner(
        out_owned_extractor, error, [&] { return check_sources({stop_words}, error); },
        [&] {
            return std::make_unique<neo_jieba_textrank_v1>(
                neo_cppjieba::TextRankExtractor{to_dict_source(*stop_words, stop_words_memory_name)});
        });
}

// Reconstruct the C++ owner only inside the library that allocated it.
void neo_jieba_v1_textrank_destroy(neo_jieba_textrank_v1 *owned_extractor) {
    const auto owner = std::unique_ptr<neo_jieba_textrank_v1>{owned_extractor};
}

// A keywords result starts empty; its capacity grows with the largest fill.
neo_jieba_status_v1 neo_jieba_v1_keywords_create(neo_jieba_keywords_v1 **out_owned_keywords,
                                                 neo_jieba_error_buffer_v1 *error) {
    return publish_empty(out_owned_keywords, error);
}

// Reconstruct the C++ owner only inside the library that allocated it.
void neo_jieba_v1_keywords_destroy(neo_jieba_keywords_v1 *owned_keywords) {
    const auto owner = std::unique_ptr<neo_jieba_keywords_v1>{owned_keywords};
}

// Both rankers share one boundary; only the options and the extractor type differ.
neo_jieba_status_v1 neo_jieba_v1_tfidf_extract_into(const neo_jieba_tfidf_v1 *extractor,
                                                    const neo_jieba_engine_v1 *engine, const char *input,
                                                    uint64_t input_size, uint64_t top_n,
                                                    const neo_jieba_tfidf_options_v1 *options,
                                                    neo_jieba_workspace_v1 *workspace, neo_jieba_keywords_v1 *keywords,
                                                    neo_jieba_error_buffer_v1 *error) {
    return extract_keywords(extractor, engine, input, input_size, top_n, options, workspace, keywords, error);
}

// Both rankers share one boundary; only the options and the extractor type differ.
neo_jieba_status_v1
neo_jieba_v1_textrank_extract_into(const neo_jieba_textrank_v1 *extractor, const neo_jieba_engine_v1 *engine,
                                   const char *input, uint64_t input_size, uint64_t top_n,
                                   const neo_jieba_textrank_options_v1 *options, neo_jieba_workspace_v1 *workspace,
                                   neo_jieba_keywords_v1 *keywords, neo_jieba_error_buffer_v1 *error) {
    return extract_keywords(extractor, engine, input, input_size, top_n, options, workspace, keywords, error);
}

// Borrowing exposes the latest fill without copying; the arrays stay stable until the next fill or destroy.
neo_jieba_status_v1 neo_jieba_v1_keywords_borrow_data(const neo_jieba_keywords_v1 *keywords,
                                                      neo_jieba_borrowed_keywords_v1 *out_borrowed,
                                                      neo_jieba_error_buffer_v1 *error) {
    return borrow_result(keywords, out_borrowed, error, [](const neo_jieba_keywords_v1 &result) {
        return neo_jieba_borrowed_keywords_v1{data_or_null(result.values), result.values.size(),
                                              data_or_null(result.occurrences), result.occurrences.size()};
    });
}
