#pragma once

#include "neo/value/PosTag.hpp"
#include "neo/value/Rune.hpp"

#include <concepts>
#include <span>
#include <type_traits>

namespace neo_cppjieba::detail {

// Looks up the tag of one complete word, as Jieba::lookup_tag does; called once per distinct candidate.
template <typename TagOf>
concept TagLookup = std::invocable<const TagOf &, std::span<const Rune>>
                    && std::same_as<std::invoke_result_t<const TagOf &, std::span<const Rune>>, PosTag>;

} // namespace neo_cppjieba::detail
