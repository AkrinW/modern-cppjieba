#pragma once

#include "neo/Unicode.hpp"
#include "neo/detail/Dag.hpp"
#include "neo/detail/DictTrie.hpp"
#include "neo/detail/Logging.hpp"
#include "neo/detail/SegmentScratch.hpp"
#include "neo/detail/StringUtil.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace neo_cppjieba {

namespace detail {

/// Check a word partition whose rune span starts at a caller-provided global offset.
[[nodiscard]] inline auto valid_segment_partition_at(std::span<const WordRange> words, size_t rune_count, uint32_t pos)
    -> bool {
    auto next = pos;
    for (const auto &word : words) {
        if (word.begin != next || word.begin >= word.end || word.end - pos > rune_count) {
            return false;
        }
        next = word.end;
    }
    return next - pos == rune_count;
}

/// Check that local word ranges form a nonempty-token partition of the rune span.
[[nodiscard]] inline auto valid_segment_partition(std::span<const WordRange> words, size_t rune_count) -> bool {
    return valid_segment_partition_at(words, rune_count, 0);
}

/// Select local result construction or appending ranges with global rune offsets.
enum class MPOutput : uint8_t { Local, Append };

/// Run MP segmentation on a separator-free rune span and keep the DAG for downstream reuse.
/// The caller owns the DAG and destination; only the DP scratch is local to this call.
// DP storage now belongs to the caller's reusable algorithm scratch.
template <MPOutput output>
inline auto mp_cut_dag(const DictTrie &dict, const Dag &dag, std::vector<WordRange> &words, uint32_t pos,
                       SegmentScratch &scratch) -> void {
    assert_check([&] { return dag.size() <= std::numeric_limits<uint32_t>::max() - pos; },
                 "MPSegment: global DAG offsets overflow");
    if constexpr (output == MPOutput::Local) {
        assert_check([&] { return pos == 0 && words.empty(); }, "MPSegment: local output must start empty at zero");
    }
    const auto n = dag.size();
    if (n == 0) {
        return;
    }
    const auto appended_begin = words.size();

    // DP node: cumulative best weight from position i to end, and the next_pos chosen by that optimal edge.
    auto &dp = scratch.route;
    // Only the first n rows are active; retaining the high-water size avoids reinitializing grown spans.
    if (dp.size() < n) {
        dp.resize(n);
    }

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
        dp[i] = MPNode{-std::numeric_limits<float>::infinity(), 0};
        const auto edges = dag.get_edges(i);
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
    if constexpr (output == MPOutput::Local) {
        words.reserve(n);
    }
    for (auto i = uint32_t{0}; i < n;) {
        auto next = dp[i].next_pos;
        assert_check([&] { return i < next && next <= n; }, "MPSegment: traceback must advance within the rune span");
        if constexpr (output == MPOutput::Local) {
            words.push_back(WordRange{i, next});
        } else {
            words.push_back(WordRange{pos + i, pos + next});
        }
        i = next;
    }

    assert_check(
        [&] { return valid_segment_partition_at(std::span<const WordRange>{words}.subspan(appended_begin), n, pos); },
        "MPSegment: words must cover the rune span exactly once");
}

/// Materialize local MP words together with the DAG needed by MIX and SEARCH.
inline auto mp_cut_segment(const DictTrie &dict, std::span<const Rune> runes, SegmentScratch &scratch) -> void {
    assert_check([&] { return runes.size() <= std::numeric_limits<uint32_t>::max(); },
                 "MPSegment: rune count exceeds the word-range limit");
    scratch.mp_words.clear();
    dict.find_dag_into(runes, scratch.dag);
    assert_check([&] { return scratch.dag.size() == runes.size(); }, "MPSegment: DAG size must match the rune span");
    mp_cut_dag<MPOutput::Local>(dict, scratch.dag, scratch.mp_words, 0, scratch);
}

/// Append MP segmentation results for a separator-free segment with a caller-provided global offset.
inline auto mp_cut_one_segment(const DictTrie &dict, std::vector<WordRange> &result, std::span<const Rune> runes,
                               uint32_t pos, SegmentScratch &scratch) -> void {
    assert_check([&] { return runes.size() <= std::numeric_limits<uint32_t>::max() - pos; },
                 "MPSegment: global word offsets overflow");
    if (runes.empty()) {
        return;
    }
    dict.find_dag_into(runes, scratch.dag);
    assert_check([&] { return scratch.dag.size() == runes.size(); }, "MPSegment: DAG size must match the rune span");
    mp_cut_dag<MPOutput::Append>(dict, scratch.dag, result, pos, scratch);
}

/// Append MP segmentation results while preserving separator runes as standalone tokens.
inline auto mp_cut_append(const DictTrie &dict, std::span<const Rune> runes, std::vector<WordRange> &range,
                          uint32_t pos, SegmentScratch &scratch) -> void {
    assert_check([&] { return runes.size() <= std::numeric_limits<uint32_t>::max() - pos; },
                 "MPSegment: global word offsets overflow");
    get_pre_filter_separators(runes, scratch.separators);
    const auto &segments = scratch.separators;
    auto segment_pos = pos;
    // First text segment before the first separator.
    mp_cut_one_segment(dict, range, runes.subspan(0, segments[0]), segment_pos, scratch);
    for (auto i = size_t{0}; i < segments.size() - 1; ++i) {
        // Emit the separator rune itself.
        range.push_back(WordRange{pos + segments[i], pos + segments[i] + 1});
        auto next_begin = segments[i] + 1;
        segment_pos = pos + next_begin;
        // Continue with the following text segment.
        mp_cut_one_segment(dict, range, runes.subspan(next_begin, segments[i + 1] - next_begin), segment_pos, scratch);
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
        auto scratch = detail::SegmentScratch{};
        cut_into(dict, runes, range, scratch);
        return range;
    }

    // Replace output while reusing scratch across every separator-free segment.
    static auto cut_into(const DictTrie &dict, std::span<const Rune> runes, std::vector<WordRange> &range,
                         detail::SegmentScratch &scratch) -> void {
        assert_check([&] { return runes.size() <= std::numeric_limits<uint32_t>::max(); },
                     "MPSegment: input exceeds the word-range limit");
        range.clear();
        range.reserve(runes.size() / 2);
        detail::mp_cut_append(dict, runes, range, 0, scratch);
    }
};

} // namespace neo_cppjieba
