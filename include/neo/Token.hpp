#pragma once

#include "neo/TaggedToken.hpp"
#include "neo/TokenView.hpp"
#include "neo/Traits.hpp"
#include "neo/detail/Tokens.hpp"

#include <string>
#include <string_view>

namespace neo_cppjieba {

// Owns token positions and borrows the input text, which must remain alive and unchanged while viewed.
// Tagging::Pos results iterate as TaggedTokenView instead of TokenView.
template <CharType CharT, Tagging tagging = Tagging::None>
using Tokens = detail::BasicTokens<std::basic_string_view<CharT>, detail::token_record_t<tagging>>;

// Owns both text and token positions; obtain fresh views after moving the result.
template <CharType CharT, Tagging tagging = Tagging::None>
using OwnedTokens = detail::BasicTokens<std::basic_string<CharT>, detail::token_record_t<tagging>>;

template <CharType CharT>
using TaggedTokens = Tokens<CharT, Tagging::Pos>;

template <CharType CharT>
using OwnedTaggedTokens = OwnedTokens<CharT, Tagging::Pos>;

} // namespace neo_cppjieba
