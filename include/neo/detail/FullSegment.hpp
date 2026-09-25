#pragma once

#include "neo/Unicode.hpp"
#include "neo/detail/Dag.hpp"
#include "neo/detail/DictTrie.hpp"
#include "neo/detail/Logging.hpp"
#include "neo/detail/SegmentScratch.hpp"
#include "neo/detail/StringUtil.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace neo_cppjieba {

/// Stateless full-mode segmentation operator.
///
/// Full mode ("全模式") outputs ALL possible dictionary words present in the
/// input sentence. Single characters that are not covered by any dictionary
/// word are also emitted as fallback tokens.
///
/// This is a purely static utility — no instance state, no ownership of
/// dictionaries. The DictTrie is taken as a const reference parameter.
struct FullSegment {
    [[nodiscard]] static auto cut(const DictTrie &dict, std::span<const Rune> runes) -> std::vector<WordRange> {
        auto range = std::vector<WordRange>{};
        auto scratch = detail::SegmentScratch{};
        cut_into(dict, runes, range, scratch);
        return range;
    }

    // Replace output and reuse the DAG between segments and calls.
    static auto cut_into(const DictTrie &dict, std::span<const Rune> runes, std::vector<WordRange> &range,
                         detail::SegmentScratch &scratch) -> void {
        assert_check([&] { return runes.size() <= std::numeric_limits<uint32_t>::max(); },
                     "FullSegment: input exceeds the word-range limit");
        range.clear();
        range.reserve(runes.size() / 2);
        get_pre_filter_separators(runes, scratch.separators);
        const auto &segments = scratch.separators;
        auto pos = uint32_t{0};
        // first segment.
        cut_one_segment(dict, range, runes.subspan(pos, segments[0] - pos), pos, scratch);
        for (auto i = size_t{0}; i < segments.size() - 1; ++i) {
            // separator segment.
            range.push_back(WordRange{segments[i], segments[i] + 1});
            pos = segments[i] + 1;
            // next text segment.
            cut_one_segment(dict, range, runes.subspan(pos, segments[i + 1] - pos), pos, scratch);
        }
    }

private:
    /// Emit all dictionary matches inside one separator-free segment, plus uncovered single-rune fallbacks.
    static auto cut_one_segment(const DictTrie &dict, std::vector<WordRange> &result, std::span<const Rune> runes,
                                uint32_t pos, detail::SegmentScratch &scratch) -> void {
        assert_check([&] { return runes.size() <= std::numeric_limits<uint32_t>::max() - pos; },
                     "FullSegment: global word offsets overflow");
        if (runes.empty()) {
            return;
        }

        dict.find_dag_into(runes, scratch.dag);
        const auto &dag = scratch.dag;
        auto n = dag.size();
        assert_check([&] { return n == runes.size(); }, "FullSegment: DAG size must match the rune span");

        // max_covered tracks the furthest rune position covered by previously emitted dictionary words; used to decide
        // whether to emit single-char fallback tokens.
        auto max_covered = size_t{0};

        for (auto i = size_t{0}; i < n; ++i) {
            auto edges = dag.get_edges(i);
            auto edge_count = edges.size();
            assert_check([&] { return !edges.empty() && edges.front().next_pos == i + 1; },
                         "FullSegment: each DAG position must start with a single-rune edge");

            if (edge_count == 1) {
                // Only one edge means this position has no longer dictionary match.
                // Emit it only when it is not already covered by an earlier overlapping word.
                if (max_covered <= i) {
                    result.push_back(WordRange{pos + static_cast<uint32_t>(i), pos + edges[0].next_pos});
                    max_covered = static_cast<size_t>(edges[0].next_pos);
                }
            } else {
                // Multiple edges means there are overlapping dictionary words starting here.
                // Full mode keeps every longer match so downstream callers can decide how to use them.
                for (auto j = size_t{1}; j < edge_count; ++j) {
                    assert_check([&] { return edges[j - 1].next_pos < edges[j].next_pos && edges[j].next_pos <= n; },
                                 "FullSegment: DAG edge targets must increase within the rune span");
                    result.push_back(WordRange{pos + static_cast<uint32_t>(i), pos + edges[j].next_pos});
                }

                // Edges are sorted from shortest to longest, so the last one extends farthest.
                max_covered = std::max(max_covered, static_cast<size_t>(edges.back().next_pos));
            }
        }
    }
};

} // namespace neo_cppjieba
