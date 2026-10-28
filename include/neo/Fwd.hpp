#pragma once

#include "neo/concept/Text.hpp"

#include <cstdint>

// Forward declarations of every public type, grouped by role: include this header to name them in declarations,
// and a facade header to use them. Compile-time settings and capacity types live in Config.hpp; alias templates
// such as Tokens need complete definitions and come with Jieba.hpp.

namespace neo_cppjieba {

// Facades.
class Jieba;
class TfIdfExtractor;
class TextRankExtractor;
class Utf;

// Runtime options.
enum class CutMode : std::uint8_t;
enum class Tagging : std::uint8_t;
struct TfIdfOptions;
struct TextRankOptions;

// Workspaces.
class Workspace;
class KeywordWorkspace;

// Values: call inputs and results. Rune is an alias, so this declaration is also its definition.
using Rune = char32_t;
struct WordRange;
struct UnicodeWithOffset;
struct SourceRange;
struct TokenPosition;
template <CharType CharT>
struct TokenView;
struct TaggedPosition;
template <CharType CharT>
struct TaggedWord;
template <CharType CharT>
struct TaggedTokenView;
template <CharType CharT>
struct Keyword;
class PosTag;
class DictSource;

} // namespace neo_cppjieba
