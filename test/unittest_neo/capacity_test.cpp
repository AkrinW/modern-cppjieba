#include "../TestUtils.hpp"
#include "gtest/gtest.h"
#include "neo/Config.hpp"
#include "neo/Jieba.hpp"
#include "neo/TokenView.hpp"
#include "neo/Unicode.hpp"
#include "neo/Workspace.hpp"
#include "neo/detail/DictTrie.hpp"
#include "neo/detail/MPSegment.hpp"
#include "neo/detail/SegmentScratch.hpp"
#include "neo/detail/Trie.hpp"

#include "test_paths.h"

#include <array>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using namespace neo_cppjieba;

namespace {

// Each executable shares immutable models while using one consistent capacity configuration.
auto capacity_jieba() -> const Jieba & {
    static const auto jieba = Jieba{DICT_DIR "/jieba.dict.utf8", DICT_DIR "/hmm_model.utf8", ""};
    return jieba;
}

} // namespace

TEST(CapacityTest, EveryModePreservesRuneAndSourcePositionsPast127) {
    const auto input = std::string(200, ' ') + "𠮷 😀";
    const auto modes = std::array{CutMode::MIX,           CutMode::MIX_NO_HMM, CutMode::FULL, CutMode::SEARCH,
                                  CutMode::SEARCH_NO_HMM, CutMode::HMM,        CutMode::MP};
    auto workspace = Workspace{};
    auto positions = std::vector<TokenPosition>{};
    for (const auto mode : modes) {
        capacity_jieba().cut_into(input, mode, positions, workspace);
        ASSERT_EQ(positions.size(), 203u);
        EXPECT_EQ(positions[200], (TokenPosition{{200, 201}, {200, 204}}));
        EXPECT_EQ(positions[201], (TokenPosition{{201, 202}, {204, 205}}));
        EXPECT_EQ(positions[202], (TokenPosition{{202, 203}, {205, 209}}));
    }
}

TEST(CapacityTest, Utf8OffsetsPreserveTheEightBitEndpoint) {
    const auto input = std::string{"𠮷"} + std::string(251, ' ');
    const auto decoded = decode_with_offset(input);
    ASSERT_EQ(decoded.runes.size(), 252u);
    EXPECT_EQ(decoded.offsets.back(), SourceOffset{255});
    EXPECT_EQ(encode(std::string_view{input}, decoded.offsets, WordRange{251, 252}), " ");
}

TEST(CapacityTest, RuneRangesPreserveTheEightBitEndpoint) {
    const auto runes = Unicode(255, U' ');
    const auto words = capacity_jieba().cut_runes(runes, CutMode::MP);
    ASSERT_EQ(words.size(), 255u);
    EXPECT_EQ(words.back(), (WordRange{254, 255}));
}

TEST(CapacityTest, NoHmmPreservesWordsEndingAtTheEightBitRuneLimit) {
    auto runes = Unicode(252, U' ');
    runes.insert(runes.end(), {U'北', U'京', U'😀'});
    const auto words = capacity_jieba().cut_runes(runes, CutMode::MIX_NO_HMM);

    ASSERT_EQ(words.size(), 254u);
    EXPECT_EQ(words[252], (WordRange{252, 254}));
    EXPECT_EQ(words.back(), (WordRange{254, 255}));
}

TEST(CapacityTest, MixPreservesHmmRunsEndingAtTheEightBitRuneLimit) {
    auto runes = Unicode(249, U' ');
    runes.insert(runes.end(), {U'杭', U'研', U'北', U'京', U'杭', U'研'});
    const auto words = capacity_jieba().cut_runes(runes, CutMode::MIX);

    ASSERT_EQ(words.size(), 252u);
    EXPECT_EQ(words[249], (WordRange{249, 251}));
    EXPECT_EQ(words[250], (WordRange{251, 253}));
    EXPECT_EQ(words[251], (WordRange{253, 255}));
}

TEST(CapacityTest, AppendedWordsPreserveTheConfiguredRuneLimit) {
    const auto dict = DictTrie{DICT_DIR "/jieba.dict.utf8", "", DictTrie::UserWordWeightOption::WordWeightMedian};
    const auto runes = Unicode{U'𠮷', U'😀'};
    const auto limit = std::numeric_limits<RuneIndex>::max();
    const auto pos = static_cast<RuneIndex>(limit - 2);
    auto words = std::vector<WordRange>{};
    auto scratch = detail::SegmentScratch{};
    detail::mp_cut_one_segment(dict, words, runes, pos, scratch);
    ASSERT_EQ(words.size(), 2u);
    EXPECT_EQ(words.front(), (WordRange{pos, static_cast<RuneIndex>(limit - 1)}));
    EXPECT_EQ(words.back(), (WordRange{static_cast<RuneIndex>(limit - 1), limit}));
}

TEST(CapacityTest, DagOffsetsPreserve253OverlappingMatches) {
    auto keys = std::vector<Unicode>{};
    keys.reserve(22);
    for (auto length = std::size_t{1}; length <= 22; ++length) {
        keys.emplace_back(length, U'甲');
    }
    auto value = DictUnit{};
    value.weight = -1.0f;
    const auto values = std::vector<DictUnit>(keys.size(), value);
    auto trie = Trie{};
    trie.build(keys, values);
    const auto dag = trie.find_dag(keys.back());
    ASSERT_EQ(dag.edges.size(), 253u);
    EXPECT_EQ(dag.offsets.back(), DagOffset{253});
    EXPECT_EQ(dag.get_edges(0).size(), 22u);
    ASSERT_EQ(dag.get_edges(21).size(), 1u);
    EXPECT_EQ(dag.get_edges(21).front().next_pos, RuneIndex{22});
}

TEST(CapacityTest, MatchTraversalPreservesTheEightBitRuneEndpoint) {
    const auto dict = DictTrie{DICT_DIR "/jieba.dict.utf8", "", DictTrie::UserWordWeightOption::WordWeightMedian};
    auto runes = Unicode(253, U' ');
    runes.insert(runes.end(), {U'北', U'京'});
    auto ends = std::vector<RuneIndex>{};
    dict.for_each_match_from(runes, 253, [&](RuneIndex end, const DictUnit &) { ends.push_back(end); });

    EXPECT_EQ(ends, (std::vector<RuneIndex>{254, 255}));
}

TEST(CapacityTest, PublicEncoderRejectsOffsetsBeyondTheHostAddressRange) {
    if constexpr (std::numeric_limits<SourceOffset>::digits > std::numeric_limits<std::size_t>::digits) {
        const auto host_limit = static_cast<SourceOffset>(std::numeric_limits<std::size_t>::max());
        const auto offsets = std::array<SourceOffset, 2>{0, static_cast<SourceOffset>(host_limit + 1)};
        EXPECT_THROW(encode(std::string_view{"a"}, offsets, WordRange{0, 1}), LogConfig::Exception);
    }
}

TEST(CapacityTest, PublicEncoderRejectsRuneRangesBeyondTheHostAddressRange) {
    if constexpr (std::numeric_limits<RuneIndex>::digits > std::numeric_limits<std::size_t>::digits) {
        const auto host_limit = static_cast<RuneIndex>(std::numeric_limits<std::size_t>::max());
        const auto offsets = std::array<SourceOffset, 2>{0, 1};
        const auto range = WordRange{0, static_cast<RuneIndex>(host_limit + 1)};
        EXPECT_THROW(encode(std::string_view{"a"}, offsets, range), LogConfig::Exception);
    }
}
