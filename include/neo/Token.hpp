#pragma once

#include "neo/TokenView.hpp"
#include "neo/Traits.hpp"
#include "neo/detail/Tokens.hpp"

#include <string>
#include <string_view>

namespace neo_cppjieba {

// Owns token positions and borrows the input text, which must remain alive and unchanged while viewed.
template <CharType CharT>
using Tokens = detail::BasicTokens<std::basic_string_view<CharT>>;

// Owns both text and token positions; obtain fresh views after moving the result.
template <CharType CharT>
using OwnedTokens = detail::BasicTokens<std::basic_string<CharT>>;

} // namespace neo_cppjieba
