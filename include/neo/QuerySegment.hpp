#pragma once

#include "DictTrie.hpp"
#include "HMModel.hpp"
#include "MixSegment.hpp"
#include "StringUtil.hpp"
#include "Unicode.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace neo_cppjieba {

/// Stateless query-mode segmentation operator.
///
/// Query mode ("搜索引擎模式") is designed for search-engine indexing. It first
/// runs MixSegment to obtain an initial word sequence, then for each word longer
/// than 2 characters, it additionally emits all 2-gram and 3-gram sub-words that
/// exist in the dictionary. This produces more fine-grained tokens that improve
/// search recall.
///
///   Example: "中国科学院" → "中国/科学/学院/科学院/中国科学院"
///
/// This is a purely static utility — no instance state, no ownership of
/// dictionaries or models. The DictTrie and HMModel are taken as const
/// reference parameters.
template <bool hmm = true>
struct QuerySegment {
    [[nodiscard]] static auto cut(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes)
        -> std::vector<WordRange> {
        auto result = std::vector<WordRange>{};
        result.reserve(runes.size());
        cut(dict, model, runes, result);
        return result;
    }

private:
    /// Append query-mode segmentation results while preserving separator runes as standalone tokens.
    static auto cut(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes, std::vector<WordRange> &result,
                    uint32_t pos = 0) -> void {
        auto segments = get_pre_filter_separators(runes);
        auto segment_pos = pos;

        // First text segment before the first separator.
        cut_one_segment_with_inline_dag(dict, model, runes.subspan(0, segments[0]), segment_pos, result);
        for (auto i = size_t{0}; i < segments.size() - 1; ++i) {
            // Emit the separator rune itself.
            result.push_back(WordRange{pos + segments[i], pos + segments[i] + 1});
            auto next_begin = segments[i] + 1;
            segment_pos = pos + next_begin;
            // Continue with the following text segment.
            cut_one_segment_with_inline_dag(dict, model, runes.subspan(next_begin, segments[i + 1] - next_begin),
                                            segment_pos, result);
        }
    }

    /// Check whether the DAG contains a dictionary match covering [begin, end).
    [[nodiscard]] static auto has_dag_edge(const Dag &dag, uint32_t begin, uint32_t end) -> bool {
        for (auto &&edge : dag.get_edges(begin)) {
            if (edge.next_pos == end) {
                return true;
            }
        }
        return false;
    }

    /// Reuse the already-built DAG to emit 2-gram and 3-gram dictionary sub-words for an MP word.
    static auto append_sub_words_from_dag(const Dag &dag, uint32_t segment_offset, WordRange word,
                                          std::vector<WordRange> &result) -> void {
        auto len = word.size();
        auto local_begin = word.begin - segment_offset;

        if (len > 2) {
            for (auto i = uint32_t{0}; i + 2 <= len; ++i) {
                auto begin = local_begin + i;
                auto end = begin + 2;
                if (has_dag_edge(dag, begin, end)) {
                    result.push_back(WordRange{word.begin + i, word.begin + i + 2});
                }
            }
        }

        if (len > 3) {
            for (auto i = uint32_t{0}; i + 3 <= len; ++i) {
                auto begin = local_begin + i;
                auto end = begin + 3;
                if (has_dag_edge(dag, begin, end)) {
                    result.push_back(WordRange{word.begin + i, word.begin + i + 3});
                }
            }
        }
    }

    /// Emit query-mode tokens for an HMM word that has no DAG path in the MP result.
    static auto append_query_local_word_by_lookup(const DictTrie &dict, std::span<const Rune> runes, WordRange word,
                                                  uint32_t segment_offset, std::vector<WordRange> &result) -> void {
        auto len = word.size();

        if (len > 2) {
            for (auto i = uint32_t{0}; i + 2 <= len; ++i) {
                auto begin = word.begin + i;
                auto sub = runes.subspan(begin, 2);
                if (dict.find(sub).has_value()) {
                    result.push_back(WordRange{segment_offset + begin, segment_offset + begin + 2});
                }
            }
        }

        if (len > 3) {
            for (auto i = uint32_t{0}; i + 3 <= len; ++i) {
                auto begin = word.begin + i;
                auto sub = runes.subspan(begin, 3);
                if (dict.find(sub).has_value()) {
                    result.push_back(WordRange{segment_offset + begin, segment_offset + begin + 3});
                }
            }
        }
        result.push_back(WordRange{segment_offset + word.begin, segment_offset + word.end});
    }

    /// Emit the main word plus its searchable sub-words when the MP DAG is already available.
    static auto append_query_word_from_dag(const Dag &dag, uint32_t segment_offset, WordRange word,
                                           std::vector<WordRange> &result) -> void {
        append_sub_words_from_dag(dag, segment_offset, word, result);
        result.push_back(word);
    }

    /// Query mode piggybacks on MP segmentation so it can reuse the DAG for sub-word generation.
    static auto cut_one_segment_with_inline_dag(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes,
                                                uint32_t pos, std::vector<WordRange> &result) -> void {
        if (runes.empty()) {
            return;
        }

        auto mp_result = detail::mp_cut_segment(dict, runes);
        auto &dag = mp_result.dag;
        auto &mp_words = mp_result.words;

        if constexpr (!hmm) {
            for (auto &word : mp_words) {
                append_query_word_from_dag(dag, pos, WordRange{pos + word.begin, pos + word.end}, result);
            }
            return;
        }

        auto i = size_t{0};
        while (i < mp_words.size()) {
            auto &word = mp_words[i];

            if (word.size() > 1 || (word.size() == 1 && dict.is_user_dict_single_chinese_word(runes[word.begin]))) {
                append_query_word_from_dag(dag, pos, WordRange{pos + word.begin, pos + word.end}, result);
                ++i;
                continue;
            }

            auto j = i;
            while (j < mp_words.size() && mp_words[j].size() == 1
                   && !dict.is_user_dict_single_chinese_word(runes[mp_words[j].begin])) {
                ++j;
            }

            auto run_begin = mp_words[i].begin;
            auto run_end = mp_words[j - 1].end;
            auto hmm_words = HMMSegment::cut(model, runes.subspan(run_begin, run_end - run_begin));
            for (auto &hmm_word : hmm_words) {
                auto local = WordRange{run_begin + hmm_word.begin, run_begin + hmm_word.end};
                append_query_local_word_by_lookup(dict, runes, local, pos, result);
            }

            i = j;
        }
    }
};

} // namespace neo_cppjieba
