#pragma once

#include "neo/Config.hpp"
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

// Receives one segmented word as a rune range; building blocks deliver words in input order.
template <typename Emit>
concept WordEmitter = std::invocable<const Emit &, WordRange>;

// Receives one word of a dictionary block together with the block's first rune, which search mode needs.
template <typename Emit>
concept BlockWordEmitter = std::invocable<const Emit &, WordRange, RuneIndex>;

// Receives each separator-free span of a text, possibly empty, with its offset in runes.
template <typename OnText>
concept SegmentVisitor = std::invocable<OnText &, std::span<const Rune>, RuneIndex>;

// Receives the rune offset of each separator between those spans.
template <typename OnSeparator>
concept SeparatorVisitor = std::invocable<OnSeparator &, RuneIndex>;

// Visits the candidate words that start at one rune: for_each_edge(begin, emit) calls emit(end, weight).
// The check passes a plain function as emit; real emitters are lambdas with the same call signature.
template <typename ForEachEdge>
concept RouteEdges = std::invocable<const ForEachEdge &, RuneIndex, void (*)(RuneIndex, WordWeight)>;

// One policy per CutMode behavior, selected together by a SegmentationStyle.
template <typename Family>
concept SegmentFamily =
    SegmentPolicy<typename Family::template Precise<true>> && SegmentPolicy<typename Family::template Precise<false>>
    && SegmentPolicy<typename Family::template Search<true>> && SegmentPolicy<typename Family::template Search<false>>
    && SegmentPolicy<typename Family::Full> && SegmentPolicy<typename Family::Hmm>;

} // namespace neo_cppjieba::detail
