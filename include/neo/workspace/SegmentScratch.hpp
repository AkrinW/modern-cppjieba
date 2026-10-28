#pragma once

#include "neo/Config.hpp"
#include "neo/resource/HMModel.hpp"
#include "neo/value/Rune.hpp"

#include <array>
#include <vector>

namespace neo_cppjieba::detail {

// Best continuation from a rune position; reused by MP, MIX, and SEARCH.
struct MPNode {
    WordWeight weight;
    RuneIndex next_pos;
};

// Dictionary membership for searchable two- and three-rune words at one start position.
struct SearchSubwords {
    bool bigram;
    bool trigram;
};

// Per-call algorithm storage; only the caller's workspace owns and mutates these buffers.
// Buffers keep their high-water size, so each algorithm overwrites the rows it reads.
struct SegmentScratch {
    std::vector<MPNode> route;
    std::vector<RuneIndex> separators;
    std::vector<SearchSubwords> search_subwords;
    // Typed one-byte states avoid unsigned-char aliasing of model and vector metadata.
    std::vector<std::array<HMMState, kHMMStatesNum>> hmm_path;
    // Python-style FULL collects the dictionary matches at one start position.
    std::vector<WordRange> full_matches;
};

} // namespace neo_cppjieba::detail
