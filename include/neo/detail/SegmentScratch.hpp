#pragma once

#include "neo/Config.hpp"
#include "neo/Unicode.hpp"
#include "neo/detail/Dag.hpp"
#include "neo/detail/HMModel.hpp"

#include <array>
#include <vector>

namespace neo_cppjieba::detail {

// Best continuation from a rune position; reused by MP, MIX, and SEARCH.
struct MPNode {
    float weight;
    RuneIndex next_pos;
};

// Per-call algorithm storage; only the caller's workspace owns and mutates these buffers.
struct SegmentScratch {
    Dag dag;
    std::vector<MPNode> route;
    std::vector<RuneIndex> separators;
    std::vector<WordRange> mp_words;
    std::vector<WordRange> hmm_words;
    // Typed one-byte states avoid unsigned-char aliasing of model and vector metadata.
    std::vector<std::array<HMMState, kHMMStatesNum>> hmm_path;
};

} // namespace neo_cppjieba::detail
