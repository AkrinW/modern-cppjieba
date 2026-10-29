#pragma once

#include "neo/Config.hpp"
#include "neo/resource/DictUnit.hpp"

#include <concepts>

namespace neo_cppjieba::detail {

// Receives each dictionary word that starts at one rune: its exclusive end and its entry, in increasing end order.
// The entry is borrowed for the duration of the call; unknown runes produce no call.
template <typename Visit>
concept MatchVisitor = std::invocable<Visit &, RuneIndex, const DictUnit &>;

} // namespace neo_cppjieba::detail
