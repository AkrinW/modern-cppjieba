#pragma once

#include "neo/concept/Text.hpp"
#include "neo/value/TaggedToken.hpp"
#include "neo/value/TokenView.hpp"

#include <concepts>
#include <string>
#include <type_traits>

namespace neo_cppjieba::detail {

// Reusable output contains source ranges, complete positions, or words in the source encoding.
template <typename Output, typename CharT>
concept OutputValue = CharType<CharT>
                      && (std::same_as<Output, SourceRange> || std::same_as<Output, TokenPosition>
                          || std::same_as<Output, std::basic_string<CharT>>);

// Invoke visitors as lvalues without copying them; failure is reported through exceptions.
template <typename Emit, typename View>
concept ViewVisitor = std::invocable<Emit &, View> && std::same_as<std::invoke_result_t<Emit &, View>, void>;

template <typename Emit, typename CharT>
concept TokenVisitor = CharType<CharT> && ViewVisitor<Emit, TokenView<CharT>>;

// Reusable tagged output: complete positions with tags, or independent words with tags.
template <typename Output, typename CharT>
concept TaggedOutputValue =
    CharType<CharT> && (std::same_as<Output, TaggedPosition> || std::same_as<Output, TaggedWord<CharT>>);

template <typename Emit, typename CharT>
concept TaggedTokenVisitor = CharType<CharT> && ViewVisitor<Emit, TaggedTokenView<CharT>>;

} // namespace neo_cppjieba::detail
