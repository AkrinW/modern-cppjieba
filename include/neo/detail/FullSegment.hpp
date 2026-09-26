#pragma once

#include "neo/Config.hpp"
#include "neo/Unicode.hpp"
#include "neo/detail/DictTrie.hpp"
#include "neo/detail/SegmentScratch.hpp"
#include "neo/detail/StringUtil.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
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
    // The DAG path is now replaced by direct matching; only separator storage is reused here.
    static auto cut_into(const DictTrie &dict, std::span<const Rune> runes, std::vector<WordRange> &range,
                         detail::SegmentScratch &scratch) -> void {
        assert(runes.size() <= std::numeric_limits<RuneIndex>::max());
        range.clear();
        range.reserve(runes.size() / 2);
        get_pre_filter_separators(runes, scratch.separators);
        const auto &segments = scratch.separators;
        auto pos = RuneIndex{0};
        // first segment.
        cut_one_segment(dict, range, runes.subspan(pos, segments[0] - pos), pos);
        for (auto i = size_t{0}; i < segments.size() - 1; ++i) {
            // separator segment.
            range.push_back(WordRange{segments[i], static_cast<RuneIndex>(segments[i] + 1)});
            pos = segments[i] + 1;
            // next text segment.
            cut_one_segment(dict, range, runes.subspan(pos, segments[i + 1] - pos), pos);
        }
    }

private:
    /// Emit all dictionary matches inside one separator-free segment, plus uncovered single-rune fallbacks.
    static auto cut_one_segment(const DictTrie &dict, std::vector<WordRange> &result, std::span<const Rune> runes,
                                RuneIndex pos) -> void {
        // max_covered tracks the furthest rune position covered by previously emitted dictionary words; used to decide
        // whether to emit single-char fallback tokens.
        auto max_covered = size_t{0};

        for (auto i = size_t{0}; i < runes.size(); ++i) {
            // Only one edge means this position has no longer dictionary match.
            // Emit it only when it is not already covered by an earlier overlapping word.
            const auto single_end = static_cast<RuneIndex>(i + 1);
            auto longest_end = single_end;

            // Multiple edges means there are overlapping dictionary words starting here.
            // Full mode keeps every longer match so downstream callers can decide how to use them.
            dict.for_each_match_from(runes, static_cast<RuneIndex>(i), [&](RuneIndex end, const DictUnit &) {
                if (end == single_end) {
                    return;
                }
                result.push_back(WordRange{static_cast<RuneIndex>(pos + i), static_cast<RuneIndex>(pos + end)});
                longest_end = end;
            });
            if (longest_end == single_end && max_covered <= i) {
                result.push_back(WordRange{static_cast<RuneIndex>(pos + i), static_cast<RuneIndex>(pos + single_end)});
            }

            // Edges are sorted from shortest to longest, so the last one extends farthest.
            max_covered = std::max(max_covered, static_cast<size_t>(longest_end));
        }
    }
};

} // namespace neo_cppjieba
