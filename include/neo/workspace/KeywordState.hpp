#pragma once

#include "neo/resource/KeywordResource.hpp"
#include "neo/third_party/gtl.hpp"
#include "neo/value/PosTag.hpp"
#include "neo/value/Rune.hpp"
#include "neo/value/TokenView.hpp"
#include "neo/workspace/Workspace.hpp"

#include <cstddef>
#include <functional>
#include <string_view>
#include <vector>

namespace neo_cppjieba::detail {

// A distinct candidate word; ids follow first occurrence, which also breaks score ties.
struct KeywordCandidate {
    TokenPosition first;
    double weight;
    PosTag tag;
};

struct KeywordOccurrence {
    size_t candidate;
    TokenPosition position;
};

// A directed TextRank edge between node ranks, which order words as CppJieba's std::map<std::string> does.
struct KeywordEdge {
    size_t from;
    size_t to;
    double weight;
};

// Per-call keyword extraction buffers behind the opaque KeywordWorkspace; capacity is retained across calls.
// Candidate keys in index borrow decoded, so the index is cleared before decoded is reused.
struct KeywordState {
    Workspace segment;
    UnicodeWithOffset decoded;
    std::vector<WordRange> ranges;
    gtl::flat_hash_map<std::u32string_view, size_t, RuneStringHash, std::equal_to<>> index;
    std::vector<KeywordCandidate> candidates;
    std::vector<KeywordOccurrence> occurrences;
    std::vector<size_t> order;
    std::vector<size_t> slots;
    std::vector<size_t> ranks;
    std::vector<KeywordEdge> edges;
    std::vector<size_t> edge_begin;
    std::vector<double> out_sums;
    std::vector<double> scores;

    [[nodiscard]] auto word(const KeywordCandidate &candidate) const noexcept -> std::u32string_view {
        return rune_view(candidate.first.runes.slice(decoded.runes));
    }
};

} // namespace neo_cppjieba::detail
