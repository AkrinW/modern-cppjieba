#include "gtest/gtest.h"
#include "neo/Config.hpp"
#include "neo/concept/Match.hpp"
#include "neo/concept/Segment.hpp"
#include "neo/concept/Text.hpp"
#include "neo/resource/DictUnit.hpp"
#include "neo/resource/Trie.hpp"
#include "neo/util/StringUtil.hpp"
#include "neo/value/Rune.hpp"
#include "neo/value/Tokens.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace neo_cppjieba;
using namespace neo_cppjieba::detail;

namespace {

// An edge source written like the real ones: a generic emitter that receives (end, weight).
struct GenericEdges {
    auto operator()(RuneIndex begin, const auto &emit) const -> void {
        emit(static_cast<RuneIndex>(begin + 1), WordWeight{0});
    }
};

// An edge source that cannot accept an emitter.
struct EdgesWithoutEmitter {
    auto operator()(RuneIndex) const -> void {
    }
};

} // namespace

TEST(ConceptTest, MatchVisitorReceivesEndAndBorrowedEntry) {
    static_assert(MatchVisitor<void (*)(RuneIndex, const DictUnit &)>);
    static_assert(!MatchVisitor<void (*)(RuneIndex)>);
    static_assert(!MatchVisitor<void (*)(const DictUnit &, RuneIndex)>);
}

TEST(ConceptTest, WordEmittersReceiveRuneRanges) {
    static_assert(WordEmitter<void (*)(WordRange)>);
    static_assert(!WordEmitter<void (*)(RuneIndex)>);
    static_assert(BlockWordEmitter<void (*)(WordRange, RuneIndex)>);
    static_assert(!BlockWordEmitter<void (*)(WordRange)>);
}

TEST(ConceptTest, SeparatedTextVisitorsReceiveSpansAndOffsets) {
    static_assert(SegmentVisitor<void (*)(std::span<const Rune>, RuneIndex)>);
    static_assert(!SegmentVisitor<void (*)(RuneIndex)>);
    static_assert(SeparatorVisitor<void (*)(RuneIndex)>);
    static_assert(!SeparatorVisitor<void (*)(std::span<const Rune>, RuneIndex)>);
}

TEST(ConceptTest, RouteEdgesAcceptGenericEmitters) {
    static_assert(RouteEdges<GenericEdges>);
    static_assert(!RouteEdges<EdgesWithoutEmitter>);
}

TEST(ConceptTest, SourceTextParserTakesTextAndName) {
    static_assert(SourceTextParser<int (*)(std::string_view, std::string_view)>);
    static_assert(!SourceTextParser<int (*)(std::string_view)>);
}

TEST(ConceptTest, TokensStoreViewsOrOwnedStrings) {
    static_assert(TokenStorage<std::string_view> && TokenStorage<std::u16string_view>);
    static_assert(TokenStorage<std::string> && TokenStorage<std::u32string>);
    static_assert(!TokenStorage<std::vector<char>>);
    static_assert(!TokenStorage<int>);
}

TEST(ConceptTest, TextNumbersAreIntegersAndFloatingPoint) {
    static_assert(TextNumber<int> && TextNumber<double>);
    static_assert(!TextNumber<bool>);
    static_assert(!TextNumber<std::string>);
}

TEST(ConceptTest, TrieLookupsAcceptTheTrieAndItsConstView) {
    static_assert(TrieSelf<Trie> && TrieSelf<const Trie>);
    static_assert(!TrieSelf<int>);
}
