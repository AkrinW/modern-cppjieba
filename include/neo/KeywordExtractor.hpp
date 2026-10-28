#pragma once

#include "neo/Fwd.hpp"
#include "neo/Jieba.hpp"
#include "neo/algorithm/KeywordRank.hpp"
#include "neo/concept/Text.hpp"
#include "neo/options/CutMode.hpp"
#include "neo/options/KeywordOptions.hpp"
#include "neo/resource/KeywordResource.hpp"
#include "neo/util/Logging.hpp"
#include "neo/util/Unicode.hpp"
#include "neo/value/DictSource.hpp"
#include "neo/value/Keyword.hpp"
#include "neo/value/PosTag.hpp"
#include "neo/value/Rune.hpp"
#include "neo/workspace/KeywordState.hpp"
#include "neo/workspace/KeywordWorkspace.hpp"

#include <cstddef>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace neo_cppjieba {

namespace detail {

// Segment once with CutMode::MIX, then keep candidate words after stop-word and tag filtering.
template <CharType CharT>
auto segment_candidates(const Jieba &jieba, const StopWords &stop_words, std::span<const PosTag> allowed_tags,
                        std::basic_string_view<CharT> source, KeywordState &state) -> void {
    detail::decode_with_offset_into(source, state.decoded);
    jieba.cut_runes_into(state.decoded.runes, CutMode::MIX, state.ranges, state.segment);
    collect_candidates(
        stop_words, allowed_tags, [&](std::span<const Rune> word) { return jieba.lookup_tag(word); }, state);
}

} // namespace detail

// TF-IDF keywords, matching CppJieba's KeywordExtractor: segment with CutMode::MIX, drop one-rune words,
// stop words and words outside options.allowed_tags, then weight each distinct word by occurrence count times IDF.
// The extractor owns only its keyword resources. Each call takes the Jieba to segment with, so any instance,
// including a clone() with runtime edits, can be used; extraction is const and shares no mutable state.
class TfIdfExtractor {
public:
    TfIdfExtractor(std::string_view idf_path, std::string_view stop_words_path, TfIdfOptions options)
        : TfIdfExtractor(DictSource::file(idf_path), DictSource::file(stop_words_path), std::move(options)) {
    }

    TfIdfExtractor(const DictSource &idf, const DictSource &stop_words, TfIdfOptions options)
        : idf_(idf), stop_words_(stop_words), options_(std::move(options)) {
    }

    TfIdfExtractor(TfIdfExtractor &&) noexcept = default;
    auto operator=(TfIdfExtractor &&) noexcept -> TfIdfExtractor & = default;
    auto operator=(const TfIdfExtractor &) -> TfIdfExtractor & = delete;

    // Copying the IDF table and stop words stays explicit at call sites.
    [[nodiscard]] auto clone() const -> TfIdfExtractor {
        return TfIdfExtractor{*this};
    }

    template <StringLike Input>
    [[nodiscard]] auto extract(const Jieba &jieba, const Input &input, size_t top_n) const
        -> std::vector<Keyword<output_char_type_t<Input>>> {
        auto workspace = KeywordWorkspace{};
        auto result = std::vector<Keyword<output_char_type_t<Input>>>{};
        extract_into(jieba, input, top_n, result, workspace);
        return result;
    }

    // Replaces out, reusing its elements' capacity. Input must not alias out.
    template <StringLike Input>
    auto extract_into(const Jieba &jieba, const Input &input, size_t top_n,
                      std::vector<Keyword<output_char_type_t<Input>>> &out, KeywordWorkspace &workspace) const -> void {
        const auto source = detail::as_view(input);
        auto &state = detail::keyword_state(workspace);
        detail::segment_candidates(jieba, stop_words_, options_.allowed_tags, source, state);
        detail::weigh_tfidf(idf_, state);
        detail::select_keywords(source, top_n, out, state);
    }

    // Words missing from the IDF file use the average of its values.
    [[nodiscard]] auto idf_average() const noexcept -> double {
        return idf_.average();
    }

    [[nodiscard]] auto options() const noexcept -> const TfIdfOptions & {
        return options_;
    }

private:
    TfIdfExtractor(const TfIdfExtractor &other)
        : idf_(other.idf_.clone()), stop_words_(other.stop_words_.clone()), options_(other.options_) {
    }

    detail::IdfTable idf_;
    detail::StopWords stop_words_;
    TfIdfOptions options_;
};

// TextRank keywords, matching CppJieba's TextRankExtractor: candidates are filtered as in TfIdfExtractor,
// co-occurrence within the window adds undirected edges, and scores are normalized so the best is 1.
// The extractor owns only its keyword resources. Each call takes the Jieba to segment with, so any instance,
// including a clone() with runtime edits, can be used; extraction is const and shares no mutable state.
class TextRankExtractor {
public:
    TextRankExtractor(std::string_view stop_words_path, TextRankOptions options)
        : TextRankExtractor(DictSource::file(stop_words_path), std::move(options)) {
    }

    TextRankExtractor(const DictSource &stop_words, TextRankOptions options)
        : stop_words_(stop_words), options_(std::move(options)) {
        check(options_.damping >= 0.0 && options_.damping <= 1.0, "TextRankExtractor: damping {} is outside [0, 1]",
              options_.damping);
    }

    TextRankExtractor(TextRankExtractor &&) noexcept = default;
    auto operator=(TextRankExtractor &&) noexcept -> TextRankExtractor & = default;
    auto operator=(const TextRankExtractor &) -> TextRankExtractor & = delete;

    // Copying the stop words stays explicit at call sites.
    [[nodiscard]] auto clone() const -> TextRankExtractor {
        return TextRankExtractor{*this};
    }

    template <StringLike Input>
    [[nodiscard]] auto extract(const Jieba &jieba, const Input &input, size_t top_n) const
        -> std::vector<Keyword<output_char_type_t<Input>>> {
        auto workspace = KeywordWorkspace{};
        auto result = std::vector<Keyword<output_char_type_t<Input>>>{};
        extract_into(jieba, input, top_n, result, workspace);
        return result;
    }

    // Replaces out, reusing its elements' capacity. Input must not alias out.
    template <StringLike Input>
    auto extract_into(const Jieba &jieba, const Input &input, size_t top_n,
                      std::vector<Keyword<output_char_type_t<Input>>> &out, KeywordWorkspace &workspace) const -> void {
        const auto source = detail::as_view(input);
        auto &state = detail::keyword_state(workspace);
        detail::segment_candidates(jieba, stop_words_, options_.allowed_tags, source, state);
        detail::weigh_text_rank(options_, state);
        detail::select_keywords(source, top_n, out, state);
    }

    [[nodiscard]] auto options() const noexcept -> const TextRankOptions & {
        return options_;
    }

private:
    TextRankExtractor(const TextRankExtractor &other)
        : stop_words_(other.stop_words_.clone()), options_(other.options_) {
    }

    detail::StopWords stop_words_;
    TextRankOptions options_;
};

} // namespace neo_cppjieba
