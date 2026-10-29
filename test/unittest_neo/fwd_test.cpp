#include "gtest/gtest.h"
#include "neo/Fwd.hpp"

#include <cstdint>
#include <type_traits>
#include <utility>

// This file includes only Fwd.hpp: a consumer header that passes the library's types around needs nothing more.

namespace neo = neo_cppjieba;

namespace {

// Every public type can be named in a declaration without its definition.
struct Pipeline {
    const neo::Jieba *jieba;
    const neo::TfIdfExtractor *tfidf;
    const neo::TextRankExtractor *textrank;
    const neo::Utf *utf;
    neo::Workspace *workspace;
    neo::KeywordWorkspace *keyword_workspace;
    const neo::DictSource *dict;
    const neo::SnapshotSource *snapshot;
    const neo::TfIdfOptions *tfidf_options;
    const neo::TextRankOptions *textrank_options;
    const neo::TokenView<char> *token;
    const neo::TaggedTokenView<char16_t> *tagged_token;
    const neo::TaggedWord<char32_t> *tagged_word;
    const neo::Keyword<wchar_t> *keyword;
    const neo::TokenPosition *position;
    const neo::TaggedPosition *tagged_position;
    const neo::SourceRange *source;
    const neo::WordRange *runes;
    const neo::UnicodeWithOffset *decoded;
    const neo::PosTag *tag;
    neo::CutMode mode;
    neo::Tagging tagging;
};

} // namespace

TEST(FwdTest, DeclaresEveryPublicTypeWithoutDefinitions) {
    const auto pipeline = Pipeline{};
    EXPECT_EQ(pipeline.jieba, nullptr);
    EXPECT_EQ(pipeline.keyword, nullptr);
    EXPECT_EQ(std::to_underlying(pipeline.mode), 0);
    EXPECT_EQ(std::to_underlying(pipeline.tagging), 0);
}

// Option enums are complete after their opaque declarations, so they pass by value.
static_assert(std::is_same_v<std::underlying_type_t<neo::CutMode>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<neo::Tagging>, std::uint8_t>);
static_assert(std::is_same_v<neo::Rune, char32_t>);
