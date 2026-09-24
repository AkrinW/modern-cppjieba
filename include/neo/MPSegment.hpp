#pragma once

#include "Dag.hpp"
#include "DictTrie.hpp"
#include "Logging.hpp"
#include "StringUtil.hpp"
#include "Unicode.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace neo_cppjieba {

namespace detail {

struct MPSegmentResult {
    Dag dag;
    std::vector<WordRange> words;
};

/// Check that local word ranges form a nonempty-token partition of the rune span.
[[nodiscard]] inline auto valid_segment_partition(std::span<const WordRange> words, size_t rune_count) -> bool {
    auto next = uint32_t{0};
    for (const auto &word : words) {
        if (word.begin != next || word.begin >= word.end || word.end > rune_count) {
            return false;
        }
        next = word.end;
    }
    return next == rune_count;
}

/// Run MP segmentation on a separator-free rune span and keep the DAG for downstream reuse.
[[nodiscard]] inline auto mp_cut_segment(const DictTrie &dict, std::span<const Rune> runes) -> MPSegmentResult {
    assert_check([&] { return runes.size() <= std::numeric_limits<uint32_t>::max(); },
                 "MPSegment: rune count exceeds the word-range limit");
    auto result = MPSegmentResult{};
    if (runes.empty()) {
        return result;
    }

    result.dag = dict.find_dag(runes);
    const auto n = result.dag.size();
    assert_check([&] { return n == runes.size(); }, "MPSegment: DAG size must match the rune span");

    // DP node: cumulative best weight from position i to end, and the next_pos chosen by that optimal edge.
    struct DPNode {
        float weight;
        uint32_t next_pos;
    };

    auto dp = std::vector<DPNode>(n, DPNode{-std::numeric_limits<float>::infinity(), 0});

    // The zero-sentinel descriptions below are historical; unknown edges now carry kMissingWordWeight.

    // Fallback weight for single characters not found in the dictionary.
    // In the DAG, such edges carry weight == 0.0f as a sentinel; the real penalty should be the dictionary minimum
    // weight.
    auto fallback = dict.min_weight();
    assert_check([&] { return std::isfinite(fallback) && fallback <= 0.0f; },
                 "MPSegment: invalid dictionary minimum weight");
    if (fallback == 0.0f) {
        // A single-entry dictionary still needs a penalty for unknown runes: one pseudo-count out of sum + 1.
        const auto freq_sum = dict.freq_sum();
        assert_check([&] { return std::isfinite(freq_sum) && freq_sum > 0.0f; },
                     "MPSegment: invalid dictionary frequency sum");
        fallback = static_cast<float>(-std::log1p(static_cast<double>(freq_sum)));
    }

    // ── Reverse DP ───────────────────────────────────────────────────
    for (auto i = n; i > 0;) {
        --i;
        const auto edges = result.dag.get_edges(i);
        assert_check([&] { return !edges.empty() && edges.front().next_pos == i + 1; },
                     "MPSegment: each DAG position must start with a single-rune edge");
        auto best_is_dictionary_word = false;

        for (const auto &edge : edges) {
            assert_check([&] { return i < edge.next_pos && edge.next_pos <= n; },
                         "MPSegment: DAG edge must advance within the rune span");
            // weight == 0.0f ⇒ not a real dictionary word (sentinel).
            // All genuine log-weights are strictly negative.
            // Only the missing-weight sentinel uses the fallback; a real zero weight participates in scoring as-is.
            const auto is_dictionary_word = edge.weight != kMissingWordWeight;
            const auto weight = is_dictionary_word ? edge.weight : fallback;
            assert_check([&] { return std::isfinite(weight); }, "MPSegment: non-finite edge weight");

            // Add the best accumulated weight from the continuation.
            auto total = weight;
            if (edge.next_pos < n) {
                total += dp[edge.next_pos].weight;
            }
            assert_check([&] { return std::isfinite(total); }, "MPSegment: non-finite accumulated weight");

            if (total > dp[i].weight || (total == dp[i].weight && is_dictionary_word && !best_is_dictionary_word)) {
                dp[i].weight = total;
                dp[i].next_pos = edge.next_pos;
                best_is_dictionary_word = is_dictionary_word;
            }
        }
    }

    // ── Trace forward ────────────────────────────────────────────────
    result.words.reserve(n);
    for (auto i = uint32_t{0}; i < n;) {
        auto next = dp[i].next_pos;
        assert_check([&] { return i < next && next <= n; }, "MPSegment: traceback must advance within the rune span");
        result.words.push_back(WordRange{i, next});
        i = next;
    }

    assert_check([&] { return valid_segment_partition(result.words, n); },
                 "MPSegment: words must cover the rune span exactly once");
    return result;
}

/// Append MP segmentation results for a separator-free segment with a caller-provided global offset.
inline auto mp_cut_one_segment(const DictTrie &dict, std::vector<WordRange> &result, std::span<const Rune> runes,
                               uint32_t pos) -> void {
    assert_check([&] { return runes.size() <= std::numeric_limits<uint32_t>::max() - pos; },
                 "MPSegment: global word offsets overflow");
    auto segment = mp_cut_segment(dict, runes);
    for (auto &word : segment.words) {
        result.push_back(WordRange{pos + word.begin, pos + word.end});
    }
}

/// Append MP segmentation results while preserving separator runes as standalone tokens.
inline auto mp_cut_append(const DictTrie &dict, std::span<const Rune> runes, std::vector<WordRange> &range,
                          uint32_t pos = 0) -> void {
    assert_check([&] { return runes.size() <= std::numeric_limits<uint32_t>::max() - pos; },
                 "MPSegment: global word offsets overflow");
    auto segments = get_pre_filter_separators(runes);
    auto segment_pos = pos;
    // First text segment before the first separator.
    mp_cut_one_segment(dict, range, runes.subspan(0, segments[0]), segment_pos);
    for (auto i = size_t{0}; i < segments.size() - 1; ++i) {
        // Emit the separator rune itself.
        range.push_back(WordRange{pos + segments[i], pos + segments[i] + 1});
        auto next_begin = segments[i] + 1;
        segment_pos = pos + next_begin;
        // Continue with the following text segment.
        mp_cut_one_segment(dict, range, runes.subspan(next_begin, segments[i + 1] - next_begin), segment_pos);
    }
}

} // namespace detail

/// Stateless maximum-probability segmentation operator.
///
/// MP mode ("最大概率模式") uses dynamic programming on the dictionary DAG
/// to find the segmentation path with the highest total log-probability.
/// This produces the most likely word sequence according to dictionary
/// frequencies.
///
/// This is a purely static utility — no instance state, no ownership of
/// dictionaries. The DictTrie is taken as a const reference parameter.
struct MPSegment {
    /// Perform MP segmentation on a Unicode rune sequence.
    ///
    /// The input is split by default separators first. Each non-separator
    /// segment is segmented independently, while separator characters are
    /// emitted as individual tokens.
    [[nodiscard]] static auto cut(const DictTrie &dict, std::span<const Rune> runes) -> std::vector<WordRange> {
        check(runes.size() <= std::numeric_limits<uint32_t>::max(), "MPSegment: input exceeds the word-range limit");
        auto range = std::vector<WordRange>{};
        range.reserve(runes.size() / 2);
        detail::mp_cut_append(dict, runes, range);
        return range;
    }
};

} // namespace neo_cppjieba
