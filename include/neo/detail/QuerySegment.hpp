#pragma once

#include "neo/Config.hpp"
#include "neo/Unicode.hpp"
#include "neo/detail/DictTrie.hpp"
#include "neo/detail/HMMSegment.hpp"
#include "neo/detail/HMModel.hpp"
#include "neo/detail/Logging.hpp"
#include "neo/detail/MPSegment.hpp"
#include "neo/detail/SegmentScratch.hpp"
#include "neo/detail/StringUtil.hpp"

#include <cassert>
#include <cstddef>
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
    // Short-word flags now replace the stored DAG on this path.
    static auto cut_into(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes,
                         std::vector<WordRange> &result, detail::SegmentScratch &scratch) -> void {
        assert_check([&] { return runes.size() <= std::numeric_limits<RuneIndex>::max(); },
                     "QuerySegment: input exceeds the word-range limit");
        result.clear();
        result.reserve(runes.size());
        cut(dict, model, runes, result, 0, scratch);
    }

private:
    /// Append query-mode segmentation results while preserving separator runes as standalone tokens.
    static auto cut(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes,
                    std::vector<WordRange> &result, RuneIndex pos, detail::SegmentScratch &scratch) -> void {
        assert_check([&] { return runes.size() <= std::numeric_limits<RuneIndex>::max() - pos; },
                     "QuerySegment: global word offsets overflow");
        get_pre_filter_separators(runes, scratch.separators);
        const auto &segments = scratch.separators;
        auto segment_pos = pos;

        // First text segment before the first separator.
        cut_one_segment(dict, model, runes.subspan(0, segments[0]), segment_pos, result, scratch);
        for (auto i = size_t{0}; i < segments.size() - 1; ++i) {
            // Emit the separator rune itself.
            result.push_back(
                WordRange{static_cast<RuneIndex>(pos + segments[i]), static_cast<RuneIndex>(pos + segments[i] + 1)});
            auto next_begin = segments[i] + 1;
            segment_pos = pos + next_begin;
            // Continue with the following text segment.
            cut_one_segment(dict, model, runes.subspan(next_begin, segments[i + 1] - next_begin), segment_pos, result,
                            scratch);
        }
    }

    /// Check whether the DAG contains a dictionary match covering [begin, end).
    [[nodiscard]] static auto has_short_match(std::span<const detail::SearchSubwords> matches, RuneIndex begin,
                                              RuneIndex end) -> bool {
        assert(begin < end && end <= matches.size());
        // Trie construction appends dictionary edges in increasing end-position order.
        // Only two- and three-rune dictionary membership is retained here.
        const auto length = end - begin;
        assert(length == 2 || length == 3);
        return length == 2 ? matches[begin].bigram : matches[begin].trigram;
    }

    /// Reuse the already-built DAG to emit 2-gram and 3-gram dictionary sub-words for an MP word.
    // Membership now comes from flags recorded during route construction.
    static auto append_sub_words(std::span<const detail::SearchSubwords> matches, RuneIndex segment_offset,
                                 WordRange word, std::vector<WordRange> &result) -> void {
        assert_check([&] { return matches.size() <= std::numeric_limits<RuneIndex>::max() - segment_offset; },
                     "QuerySegment: global word offsets overflow");
        assert_check([&] { return segment_offset <= word.begin && word.begin < word.end; },
                     "QuerySegment: invalid global word range");
        assert_check([&] { return word.end - segment_offset <= matches.size(); },
                     "QuerySegment: word range exceeds the match flags");
        auto len = word.size();
        auto local_begin = word.begin - segment_offset;

        if (len > 2) {
            for (auto i = RuneIndex{0}; i <= len - 2; ++i) {
                auto begin = local_begin + i;
                auto end = begin + 2;
                if (has_short_match(matches, begin, end)) {
                    result.push_back(
                        WordRange{static_cast<RuneIndex>(word.begin + i), static_cast<RuneIndex>(word.begin + i + 2)});
                }
            }
        }

        if (len > 3) {
            for (auto i = RuneIndex{0}; i <= len - 3; ++i) {
                auto begin = local_begin + i;
                auto end = begin + 3;
                if (has_short_match(matches, begin, end)) {
                    result.push_back(
                        WordRange{static_cast<RuneIndex>(word.begin + i), static_cast<RuneIndex>(word.begin + i + 3)});
                }
            }
        }
    }

    /// Emit query-mode tokens for an HMM word that has no DAG path in the MP result.
    /// Its dictionary sub-words remain available in the full segment DAG, independent of the chosen MP path.
    // All start positions retain short-word flags, including positions outside the selected MP path.
    static auto append_query_local_word(std::span<const detail::SearchSubwords> matches, WordRange word,
                                        RuneIndex segment_offset, std::vector<WordRange> &result) -> void {
        assert_check([&] { return matches.size() <= std::numeric_limits<RuneIndex>::max() - segment_offset; },
                     "QuerySegment: global word offsets overflow");
        assert_check([&] { return word.begin < word.end && word.end <= matches.size(); },
                     "QuerySegment: invalid local HMM word range");
        append_query_word(matches, segment_offset,
                          WordRange{static_cast<RuneIndex>(segment_offset + word.begin),
                                    static_cast<RuneIndex>(segment_offset + word.end)},
                          result);
    }

    /// Emit the main word plus its searchable sub-words when the MP DAG is already available.
    static auto append_query_word(std::span<const detail::SearchSubwords> matches, RuneIndex segment_offset,
                                  WordRange word, std::vector<WordRange> &result) -> void {
        append_sub_words(matches, segment_offset, word, result);
        result.push_back(word);
    }

    /// Query mode piggybacks on MP segmentation so it can reuse the DAG for sub-word generation.
    // Route construction now records short-word flags and emits final words without intermediate word arrays.
    static auto cut_one_segment(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes, RuneIndex pos,
                                std::vector<WordRange> &result, detail::SegmentScratch &scratch) -> void {
        assert_check([&] { return runes.size() <= std::numeric_limits<RuneIndex>::max() - pos; },
                     "QuerySegment: global word offsets overflow");
        if (runes.empty()) {
            return;
        }

        detail::mp_build_route_from_matches<detail::MPMatchMode::Search>(dict, runes, scratch);
        assert(scratch.route.size() >= runes.size() && scratch.search_subwords.size() >= runes.size());
        const auto &route = scratch.route;
        const auto matches = std::span<const detail::SearchSubwords>{scratch.search_subwords}.first(runes.size());
        const auto emit_word = [&](WordRange word) {
            append_query_local_word(matches, word, pos, result);
        };

        if constexpr (!hmm) {
            for (auto begin = RuneIndex{0}; begin < runes.size();) {
                const auto end = route[begin].next_pos;
                assert(begin < end && end <= runes.size());
                emit_word(WordRange{begin, end});
                begin = end;
            }
            return;
        }

        // Scratch buffer for HMM segmentation — allocated once and reused
        // across iterations to avoid repeated heap allocations.
        // HMM now emits through the consumer directly, retaining only its separate Viterbi path buffer.

        auto begin = RuneIndex{0};
        while (begin < runes.size()) {
            const auto end = route[begin].next_pos;
            assert(begin < end && end <= runes.size());

            if (end - begin > 1 || dict.is_user_dict_single_chinese_word(runes[begin])) {
                emit_word(WordRange{begin, end});
                begin = end;
                continue;
            }

            const auto run_begin = begin;
            auto run_end = end;
            while (run_end < runes.size() && route[run_end].next_pos == run_end + 1
                   && !dict.is_user_dict_single_chinese_word(runes[run_end])) {
                ++run_end;
            }

            assert(run_begin < run_end);

            // Reuse hmm_scratch: clear capacity-preserving, then fill directly.
            // Call hmm_cut_one_segment instead of HMMSegment::cut to skip
            // redundant separator detection — runes are already separator-free.
            // The same separator-free HMM path now sends words directly to the consumer.
            detail::hmm_emit_one_segment(model, emit_word, runes.subspan(run_begin, run_end - run_begin), run_begin,
                                         scratch);

            begin = run_end;
        }
    }
};

} // namespace neo_cppjieba
