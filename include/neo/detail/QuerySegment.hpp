#pragma once

#include "neo/Unicode.hpp"
#include "neo/detail/DictTrie.hpp"
#include "neo/detail/HMMSegment.hpp"
#include "neo/detail/HMModel.hpp"
#include "neo/detail/Logging.hpp"
#include "neo/detail/MPSegment.hpp"
#include "neo/detail/SegmentScratch.hpp"
#include "neo/detail/StringUtil.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
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
        auto scratch = detail::SegmentScratch{};
        cut_into(dict, model, runes, result, scratch);
        return result;
    }

    // Retain the current segment's DAG for sub-words while reusing all algorithm buffers.
    static auto cut_into(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes,
                         std::vector<WordRange> &result, detail::SegmentScratch &scratch) -> void {
        check(runes.size() <= std::numeric_limits<uint32_t>::max(), "QuerySegment: input exceeds the word-range limit");
        result.clear();
        result.reserve(runes.size());
        cut(dict, model, runes, result, 0, scratch);
    }

private:
    /// Append query-mode segmentation results while preserving separator runes as standalone tokens.
    static auto cut(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes,
                    std::vector<WordRange> &result, uint32_t pos, detail::SegmentScratch &scratch) -> void {
        assert_check([&] { return runes.size() <= std::numeric_limits<uint32_t>::max() - pos; },
                     "QuerySegment: global word offsets overflow");
        get_pre_filter_separators(runes, scratch.separators);
        const auto &segments = scratch.separators;
        auto segment_pos = pos;

        // First text segment before the first separator.
        cut_one_segment_with_inline_dag(dict, model, runes.subspan(0, segments[0]), segment_pos, result, scratch);
        for (auto i = size_t{0}; i < segments.size() - 1; ++i) {
            // Emit the separator rune itself.
            result.push_back(WordRange{pos + segments[i], pos + segments[i] + 1});
            auto next_begin = segments[i] + 1;
            segment_pos = pos + next_begin;
            // Continue with the following text segment.
            cut_one_segment_with_inline_dag(dict, model, runes.subspan(next_begin, segments[i + 1] - next_begin),
                                            segment_pos, result, scratch);
        }
    }

    /// Check whether the DAG contains a dictionary match covering [begin, end).
    [[nodiscard]] static auto has_dag_edge(const Dag &dag, uint32_t begin, uint32_t end) -> bool {
        assert_check([&] { return begin < end && end <= dag.size(); }, "QuerySegment: invalid DAG lookup range");
        for (auto &&edge : dag.get_edges(begin)) {
            // Trie construction appends dictionary edges in increasing end-position order.
            if (edge.next_pos >= end) {
                return edge.next_pos == end;
            }
        }
        return false;
    }

    /// Reuse the already-built DAG to emit 2-gram and 3-gram dictionary sub-words for an MP word.
    static auto append_sub_words_from_dag(const Dag &dag, uint32_t segment_offset, WordRange word,
                                          std::vector<WordRange> &result) -> void {
        assert_check([&] { return dag.size() <= std::numeric_limits<uint32_t>::max() - segment_offset; },
                     "QuerySegment: global DAG offsets overflow");
        assert_check([&] { return segment_offset <= word.begin && word.begin < word.end; },
                     "QuerySegment: invalid global word range");
        assert_check([&] { return word.end - segment_offset <= dag.size(); },
                     "QuerySegment: word range exceeds the DAG");
        auto len = word.size();
        auto local_begin = word.begin - segment_offset;

        if (len > 2) {
            for (auto i = uint32_t{0}; i <= len - 2; ++i) {
                auto begin = local_begin + i;
                auto end = begin + 2;
                if (has_dag_edge(dag, begin, end)) {
                    result.push_back(WordRange{word.begin + i, word.begin + i + 2});
                }
            }
        }

        if (len > 3) {
            for (auto i = uint32_t{0}; i <= len - 3; ++i) {
                auto begin = local_begin + i;
                auto end = begin + 3;
                if (has_dag_edge(dag, begin, end)) {
                    result.push_back(WordRange{word.begin + i, word.begin + i + 3});
                }
            }
        }
    }

    /// Emit query-mode tokens for an HMM word that has no DAG path in the MP result.
    /// Its dictionary sub-words remain available in the full segment DAG, independent of the chosen MP path.
    static auto append_query_local_word_from_dag(const Dag &dag, WordRange word, uint32_t segment_offset,
                                                 std::vector<WordRange> &result) -> void {
        assert_check([&] { return dag.size() <= std::numeric_limits<uint32_t>::max() - segment_offset; },
                     "QuerySegment: global word offsets overflow");
        assert_check([&] { return word.begin < word.end && word.end <= dag.size(); },
                     "QuerySegment: invalid local HMM word range");
        append_query_word_from_dag(dag, segment_offset,
                                   WordRange{segment_offset + word.begin, segment_offset + word.end}, result);
    }

    /// Emit the main word plus its searchable sub-words when the MP DAG is already available.
    static auto append_query_word_from_dag(const Dag &dag, uint32_t segment_offset, WordRange word,
                                           std::vector<WordRange> &result) -> void {
        append_sub_words_from_dag(dag, segment_offset, word, result);
        result.push_back(word);
    }

    /// Query mode piggybacks on MP segmentation so it can reuse the DAG for sub-word generation.
    static auto cut_one_segment_with_inline_dag(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes,
                                                uint32_t pos, std::vector<WordRange> &result,
                                                detail::SegmentScratch &scratch) -> void {
        assert_check([&] { return runes.size() <= std::numeric_limits<uint32_t>::max() - pos; },
                     "QuerySegment: global word offsets overflow");
        if (runes.empty()) {
            return;
        }

        detail::mp_cut_segment(dict, runes, scratch);
        const auto &dag = scratch.dag;
        const auto &mp_words = scratch.mp_words;
        assert_check([&] { return dag.size() == runes.size(); }, "QuerySegment: DAG size must match the rune span");
        assert_check([&] { return detail::valid_segment_partition(mp_words, runes.size()); },
                     "QuerySegment: MP words must cover the rune span exactly once");

        if constexpr (!hmm) {
            for (auto &word : mp_words) {
                append_query_word_from_dag(dag, pos, WordRange{pos + word.begin, pos + word.end}, result);
            }
            return;
        }

        // Scratch buffer for HMM segmentation — allocated once and reused
        // across iterations to avoid repeated heap allocations.
        auto &hmm_scratch = scratch.hmm_words;

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

            assert_check([&] { return i < j; }, "QuerySegment: HMM input must contain at least one MP word");
            auto run_begin = mp_words[i].begin;
            auto run_end = mp_words[j - 1].end;

            // Reuse hmm_scratch: clear capacity-preserving, then fill directly.
            // Call hmm_cut_one_segment instead of HMMSegment::cut to skip
            // redundant separator detection — runes are already separator-free.
            hmm_scratch.clear();
            detail::hmm_cut_one_segment(model, hmm_scratch, runes.subspan(run_begin, run_end - run_begin), 0, scratch);
            assert_check([&] { return detail::valid_segment_partition(hmm_scratch, run_end - run_begin); },
                         "QuerySegment: HMM words must cover their input span exactly once");

            for (auto &hmm_word : hmm_scratch) {
                auto local = WordRange{run_begin + hmm_word.begin, run_begin + hmm_word.end};
                append_query_local_word_from_dag(dag, local, pos, result);
            }

            i = j;
        }
    }
};

} // namespace neo_cppjieba
