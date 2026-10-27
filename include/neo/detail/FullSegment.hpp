#pragma once

#include "neo/Config.hpp"
#include "neo/Unicode.hpp"
#include "neo/detail/DictTrie.hpp"
#include "neo/detail/SegmentCommon.hpp"
#include "neo/detail/SegmentScratch.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

namespace neo_cppjieba::detail {

/// Stateless full-mode segmentation operator.
///
/// Full mode ("全模式") outputs ALL possible dictionary words present in the
/// input sentence. Single characters that are not covered by any dictionary
/// word are also emitted as fallback tokens.
struct FullSegment {
    [[nodiscard]] static auto cut(const DictTrie &dict, std::span<const Rune> runes) -> std::vector<WordRange> {
        auto range = std::vector<WordRange>{};
        auto scratch = detail::SegmentScratch{};
        cut_into(dict, runes, range, scratch);
        return range;
    }

    // Replace output; only separator storage is reused between calls.
    static auto cut_into(const DictTrie &dict, std::span<const Rune> runes, std::vector<WordRange> &range,
                         detail::SegmentScratch &scratch) -> void {
        assert(detail::size_fits_capacity<RuneIndex>(runes.size()));
        range.clear();
        range.reserve(runes.size() / 2);
        detail::for_each_separated(
            runes, scratch.separators,
            [&](std::span<const Rune> segment, RuneIndex offset) { cut_one_segment(dict, range, segment, offset); },
            [&](RuneIndex separator) { range.push_back({separator, static_cast<RuneIndex>(separator + 1)}); });
    }

private:
    /// Emit all dictionary matches inside one separator-free segment, plus uncovered single-rune fallbacks.
    static auto cut_one_segment(const DictTrie &dict, std::vector<WordRange> &result, std::span<const Rune> runes,
                                RuneIndex pos) -> void {
        // max_covered tracks the furthest rune position covered by previously emitted dictionary words; used to decide
        // whether to emit single-char fallback tokens.
        auto max_covered = size_t{0};

        for (auto i = size_t{0}; i < runes.size(); ++i) {
            const auto single_end = static_cast<RuneIndex>(i + 1);
            auto longest_end = single_end;

            // Keep every multi-rune match; overlapping words are the point of full mode.
            dict.for_each_match_from(runes, static_cast<RuneIndex>(i), [&](RuneIndex end, const DictUnit &) {
                if (end == single_end) {
                    return;
                }
                result.push_back(WordRange{static_cast<RuneIndex>(pos + i), static_cast<RuneIndex>(pos + end)});
                longest_end = end;
            });
            // Without a longer match, emit the single rune unless an earlier word already covers it.
            if (longest_end == single_end && max_covered <= i) {
                result.push_back(WordRange{static_cast<RuneIndex>(pos + i), static_cast<RuneIndex>(pos + single_end)});
            }

            // Matches arrive from shortest to longest, so the last one extends farthest.
            max_covered = std::max(max_covered, static_cast<size_t>(longest_end));
        }
    }
};

} // namespace neo_cppjieba::detail
