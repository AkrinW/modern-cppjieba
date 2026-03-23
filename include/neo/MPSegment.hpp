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
        auto segments = get_pre_filter_separators(runes);
        auto pos = uint32_t{0};
        // first segment.
        cut_one_segment(dict, range, runes.subspan(pos, segments[0] - pos), pos);
        for (auto i = size_t{0}; i < segments.size() - 1; ++i) {
            // separator segment.
            range.push_back(WordRange{segments[i], segments[i] + 1});
            pos = segments[i] + 1;
            // next text segment.
            cut_one_segment(dict, range, runes.subspan(pos, segments[i + 1] - pos), pos);
        }
        return range;
    }

private:
    /// Perform MP segmentation on a separator-free Unicode rune sequence.
    static auto cut_one_segment(const DictTrie &dict, std::vector<WordRange> &result, std::span<const Rune> runes,
                                uint32_t pos) -> void {
        if (runes.empty()) {
            return;
        }

        auto dag = dict.find_dag(runes);
        auto n = dag.size();

        // DP node: cumulative best weight from position i to end, and the next_pos chosen by that optimal edge.
        struct DPNode {
            float weight;
            uint32_t next_pos;
        };

        auto dp = std::vector<DPNode>(n, DPNode{-std::numeric_limits<float>::infinity(), 0});

        // Fallback weight for single characters not found in the dictionary.
        // In the DAG, such edges carry weight == 0.0f as a sentinel; the real penalty should be the dictionary minimum
        // weight.
        auto fallback = dict.min_weight();

        // ── Reverse DP ───────────────────────────────────────────────────
        for (auto i = static_cast<ptrdiff_t>(n) - 1; i >= 0; --i) {
            auto edges = dag.get_edges(static_cast<size_t>(i));

            for (auto &&edge : edges) {
                // weight == 0.0f ⇒ not a real dictionary word (sentinel).
                // All genuine log-weights are strictly negative.
                auto weight = edge.weight == 0.0f ? fallback : edge.weight;

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
        for (auto i = uint32_t{0}; i < n;) {
            auto next = dp[i].next_pos;
            result.push_back(WordRange{pos + i, pos + next});
            i = next;
        }
    }
};

} // namespace neo_cppjieba
