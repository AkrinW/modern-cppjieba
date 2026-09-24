#pragma once

#include "Dag.hpp"
#include "DictTrie.hpp"
#include "StringUtil.hpp"
#include "Unicode.hpp"

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

/// Run MP segmentation on a separator-free rune span and keep the DAG for downstream reuse.
[[nodiscard]] inline auto mp_cut_segment(const DictTrie &dict, std::span<const Rune> runes) -> MPSegmentResult {
    auto result = MPSegmentResult{};
    if (runes.empty()) {
        return result;
    }

    result.dag = dict.find_dag(runes);
    auto n = result.dag.size();

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

    // ── Reverse DP ───────────────────────────────────────────────────
    for (auto i = static_cast<ptrdiff_t>(n) - 1; i >= 0; --i) {
        auto edges = result.dag.get_edges(static_cast<size_t>(i));

        for (auto &&edge : edges) {
            // weight == 0.0f ⇒ not a real dictionary word (sentinel).
            // All genuine log-weights are strictly negative.
            // Only the missing-weight sentinel uses the fallback; a real zero weight participates in scoring as-is.
            auto weight = edge.weight == kMissingWordWeight ? fallback : edge.weight;

            // Add the best accumulated weight from the continuation.
            auto total = weight;
            if (edge.next_pos < n) {
                total += dp[edge.next_pos].weight;
            }

            if (total > dp[static_cast<size_t>(i)].weight) {
                dp[static_cast<size_t>(i)].weight = total;
                dp[static_cast<size_t>(i)].next_pos = edge.next_pos;
            }
        }
    }

    // ── Trace forward ────────────────────────────────────────────────
    result.words.reserve(n);
    for (auto i = uint32_t{0}; i < n;) {
        auto next = dp[i].next_pos;
        result.words.push_back(WordRange{i, next});
        i = next;
    }

    return result;
}

/// Append MP segmentation results for a separator-free segment with a caller-provided global offset.
inline auto mp_cut_one_segment(const DictTrie &dict, std::vector<WordRange> &result, std::span<const Rune> runes,
                               uint32_t pos) -> void {
    auto segment = mp_cut_segment(dict, runes);
    for (auto &word : segment.words) {
        result.push_back(WordRange{pos + word.begin, pos + word.end});
    }
}

/// Append MP segmentation results while preserving separator runes as standalone tokens.
inline auto mp_cut_append(const DictTrie &dict, std::span<const Rune> runes, std::vector<WordRange> &range,
                          uint32_t pos = 0) -> void {
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
        auto range = std::vector<WordRange>{};
        range.reserve(runes.size() / 2);
        detail::mp_cut_append(dict, runes, range);
        return range;
    }
};

} // namespace neo_cppjieba
