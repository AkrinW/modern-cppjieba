#pragma once

#include "neo/Fwd.hpp"
#include "neo/concept/Text.hpp"
#include "neo/value/PosTag.hpp"
#include "neo/value/TokenView.hpp"

#include <string>
#include <vector>

namespace neo_cppjieba {

// One keyword in the input encoding with its score, its tag as Jieba::lookup_tag reports it,
// and every occurrence in input order.
// Positions use the same coordinates as Jieba::cut: source code units and runes, both half-open.
template <CharType CharT>
struct Keyword {
    std::basic_string<CharT> word;
    double weight{0.0};
    PosTag tag;
    std::vector<TokenPosition> positions;
};

} // namespace neo_cppjieba
