#pragma once

#include "neo/Config.hpp"
#include "neo/CutMode.hpp"
#include "neo/UnicodeTypes.hpp"
#include "neo/detail/DictTrie.hpp"
#include "neo/detail/FullSegment.hpp"
#include "neo/detail/HMMSegment.hpp"
#include "neo/detail/HMModel.hpp"
#include "neo/detail/Logging.hpp"
#include "neo/detail/MixSegment.hpp"
#include "neo/detail/QuerySegment.hpp"
#include "neo/detail/SegmentCommon.hpp"
#include "neo/detail/SegmentScratch.hpp"
#include "neo/detail/StyledSegment.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

// The only place where a runtime CutMode meets the compile-time SegmentationStyle.
// Each mode resolves to one algorithm family here; facades never branch on the style.

namespace neo_cppjieba::detail {

inline constexpr auto kCppStyle = compile_config::segmentation_style == SegmentationStyle::CPP;

// Precise mode: MIX with HMM, MIX_NO_HMM and MP without. MP and MIX_NO_HMM are the same route in every style.
template <bool hmm>
inline auto cut_precise(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes,
                        std::vector<WordRange> &out, SegmentScratch &scratch) -> void {
    if constexpr (kCppStyle) {
        MixSegment<hmm>::cut_into(dict, model, runes, out, scratch);
    } else {
        style_cut_mix<hmm>(dict, model, runes, out, scratch);
    }
}

template <bool hmm>
inline auto cut_search(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes,
                       std::vector<WordRange> &out, SegmentScratch &scratch) -> void {
    if constexpr (kCppStyle) {
        QuerySegment<hmm>::cut_into(dict, model, runes, out, scratch);
    } else {
        style_cut_search<hmm>(dict, model, runes, out, scratch);
    }
}

inline auto cut_full(const DictTrie &dict, std::span<const Rune> runes, std::vector<WordRange> &out,
                     SegmentScratch &scratch) -> void {
    if constexpr (kCppStyle) {
        FullSegment::cut_into(dict, runes, out, scratch);
    } else {
        style_cut_full(dict, runes, out, scratch);
    }
}

// Partition modes cover every rune once; full and search modes may emit overlapping words.
[[nodiscard]] inline auto valid_segment_result(CutMode mode, std::span<const WordRange> words,
                                               size_t rune_count) noexcept -> bool {
    if (mode == CutMode::MIX || mode == CutMode::MIX_NO_HMM || mode == CutMode::HMM || mode == CutMode::MP) {
        return valid_segment_partition(words, rune_count);
    }
    if (rune_count == 0) {
        return words.empty();
    }
    if (words.empty()) {
        // Rust FULL emits nothing for input without dictionary-block or ASCII runs.
        return compile_config::segmentation_style == SegmentationStyle::RUST && mode == CutMode::FULL;
    }
    return std::ranges::all_of(words, [=](const WordRange &word) {
        // Python FULL keeps the empty gaps between whitespace matches.
        const auto allow_empty =
            compile_config::segmentation_style == SegmentationStyle::PYTHON && mode == CutMode::FULL;
        return (word.begin < word.end || (allow_empty && word.begin == word.end)) && word.end <= rune_count;
    });
}

// Replace out with the words of a validated mode; out and scratch keep their capacity.
inline auto segment_into(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes, CutMode mode,
                         std::vector<WordRange> &out, SegmentScratch &scratch) -> void {
    switch (mode) {
        case CutMode::MIX:
            cut_precise<true>(dict, model, runes, out, scratch);
            break;
        case CutMode::MIX_NO_HMM:
        case CutMode::MP:
            cut_precise<false>(dict, model, runes, out, scratch);
            break;
        case CutMode::FULL:
            cut_full(dict, runes, out, scratch);
            break;
        case CutMode::SEARCH:
            cut_search<true>(dict, model, runes, out, scratch);
            break;
        case CutMode::SEARCH_NO_HMM:
            cut_search<false>(dict, model, runes, out, scratch);
            break;
        case CutMode::HMM:
            HMMSegment::cut_into(model, runes, out, scratch);
            break;
        default:
            assert_check([] { return false; }, "Unchecked Jieba cut mode reached internal dispatch");
            std::unreachable();
    }
    assert_check([&] { return valid_segment_result(mode, out, runes.size()); },
                 "Jieba: invalid segmentation result for {} runes", runes.size());
}

} // namespace neo_cppjieba::detail
