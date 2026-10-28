#pragma once

#include "neo/resource/DictTrie.hpp"
#include "neo/resource/HMModel.hpp"
#include "neo/value/Rune.hpp"
#include "neo/workspace/SegmentScratch.hpp"

#include <concepts>
#include <span>
#include <vector>

namespace neo_cppjieba::detail {

// A stateless segmentation policy replaces out with rune ranges of runes and reuses scratch buffers.
// Every policy takes both resources so one signature covers the family; unused parameters are unnamed
// and vanish after inlining. Partition policies cover every rune once; FULL and SEARCH may overlap.
template <typename Policy>
concept SegmentPolicy = requires(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes,
                                 std::vector<WordRange> &out, SegmentScratch &scratch) {
    { Policy::cut_into(dict, model, runes, out, scratch) } -> std::same_as<void>;
};

// One policy per CutMode behavior, selected together by a SegmentationStyle.
template <typename Family>
concept SegmentFamily =
    SegmentPolicy<typename Family::template Precise<true>> && SegmentPolicy<typename Family::template Precise<false>>
    && SegmentPolicy<typename Family::template Search<true>> && SegmentPolicy<typename Family::template Search<false>>
    && SegmentPolicy<typename Family::Full> && SegmentPolicy<typename Family::Hmm>;

} // namespace neo_cppjieba::detail
