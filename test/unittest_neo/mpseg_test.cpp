#include "../TestUtils.hpp"
#include "gtest/gtest.h"
#include "neo/Unicode.hpp"
#include "neo/detail/DictTrie.hpp"
#include "neo/detail/MPSegment.hpp"

#include "test_paths.h"

#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace neo_cppjieba;

inline constexpr auto DICT_FILE = std::string_view{DICT_DIR "/jieba.dict.utf8"};

TEST(MPSegmentTest, EmptyInput) {
    auto dict = DictTrie{DICT_FILE};
    auto runes = Unicode{};
    auto result = MPSegment::cut(dict, runes);
    EXPECT_TRUE(result.empty());
}

TEST(MPSegmentTest, SingleChar) {
    auto dict = DictTrie{DICT_FILE};
    auto runes = decode(std::string_view{"我"});
    auto result = MPSegment::cut(dict, runes);
    auto words = to_strings(runes, result);
    ASSERT_EQ(words.size(), 1);
    EXPECT_EQ(words[0], "我");
}

TEST(MPSegmentTest, ClassicSentence) {
    auto dict = DictTrie{DICT_FILE};
    auto sentence = std::string_view{"我来自北京邮电大学"};
    auto runes = decode(sentence);
    auto result = MPSegment::cut(dict, runes);
    auto words = to_strings(runes, result);

    auto expected = std::vector<std::string>{"我", "来自", "北京邮电大学"};
    EXPECT_EQ(words, expected) << "actual: " << join(words);
}

TEST(MPSegmentTest, NanjingBridge) {
    auto dict = DictTrie{DICT_FILE};
    auto runes = decode(std::string_view{"南京市长江大桥"});
    auto result = MPSegment::cut(dict, runes);
    auto words = to_strings(runes, result);

    auto expected = std::vector<std::string>{"南京市", "长江大桥"};
    EXPECT_EQ(words, expected) << "actual: " << join(words);
}

TEST(MPSegmentTest, HunanChangsha) {
    auto dict = DictTrie{DICT_FILE};
    auto runes = decode(std::string_view{"湖南长沙市天心区"});
    auto result = MPSegment::cut(dict, runes);
    auto words = to_strings(runes, result);

    EXPECT_EQ(join(words), "湖南长沙市/天心区") << "actual: " << join(words);
}

TEST(MPSegmentTest, SeparatorsAndPunctuation) {
    auto dict = DictTrie{DICT_FILE};
    auto sentence = std::string_view{"我来自北京邮电大学。"};
    auto runes = decode(sentence);
    auto result = MPSegment::cut(dict, runes);
    auto words = to_strings(runes, result);

    auto expected = std::vector<std::string>{"我", "来自", "北京邮电大学", "。"};
    EXPECT_EQ(words, expected) << "actual: " << join(words);
}

TEST(MPSegmentTest, AppendsIndependentSegmentsWithRuneOffsets) {
    const auto dict = DictTrie{DICT_FILE, "", DictTrie::UserWordWeightOption::WordWeightMedian};
    const auto runes = decode(std::string_view{"甲 \t南京市长江大桥，，𠮷😀\n的了是。乙"});
    const auto input = std::span<const Rune>{runes}.subspan(1, runes.size() - 2);
    const auto result = MPSegment::cut(dict, input);

    EXPECT_EQ(to_strings(input, result), (std::vector<std::string>{" ", "\t", "南京市", "长江大桥", "，", "，", "𠮷",
                                                                   "😀", "\n", "的", "了", "是", "。"}));
    EXPECT_EQ(result, (std::vector<WordRange>{{0, 1},
                                              {1, 2},
                                              {2, 5},
                                              {5, 9},
                                              {9, 10},
                                              {10, 11},
                                              {11, 12},
                                              {12, 13},
                                              {13, 14},
                                              {14, 15},
                                              {15, 16},
                                              {16, 17},
                                              {17, 18}}));
}

TEST(MPSegmentTest, PreservesExistingOutputWhenAppendingAtTheRuneOffsetLimit) {
    const auto dict = DictTrie{DICT_FILE, "", DictTrie::UserWordWeightOption::WordWeightMedian};
    const auto runes = decode(std::string_view{"𠮷😀"});
    const auto pos = std::numeric_limits<uint32_t>::max() - uint32_t{2};
    auto result = std::vector<WordRange>{{7, 8}};
    detail::mp_cut_one_segment(dict, result, runes, pos);

    EXPECT_EQ(result, (std::vector<WordRange>{{7, 8}, {pos, pos + 1}, {pos + 1, pos + 2}}));
}

TEST(MPSegmentTest, MixedAsciiAndChinese) {
    auto dict = DictTrie{DICT_FILE};
    auto runes = decode(std::string_view{"B超 T恤"});
    auto result = MPSegment::cut(dict, runes);
    auto words = to_strings(runes, result);

    EXPECT_EQ(join(words), "B超/ /T恤") << "actual: " << join(words);
}

TEST(MPSegmentTest, UnicodeEmojiAndPunctuation) {
    auto dict = DictTrie{DICT_FILE};
    auto runes = decode(std::string_view{"天气很好，🙋 我们去郊游。"});
    auto result = MPSegment::cut(dict, runes);
    auto words = to_strings(runes, result);

    EXPECT_EQ(join(words), "天气/很/好/，/🙋/ /我们/去/郊游/。") << "actual: " << join(words);
}

TEST(MPSegmentTest, WordRangeContiguous) {
    auto dict = DictTrie{DICT_FILE};
    auto runes = decode(std::string_view{"小明硕士毕业于中国科学院计算所"});
    auto result = MPSegment::cut(dict, runes);

    ASSERT_FALSE(result.empty());
    EXPECT_EQ(result.front().begin, 0u);

    for (size_t i = 1; i < result.size(); ++i) {
        EXPECT_EQ(result[i].begin, result[i - 1].end) << "gap between word " << (i - 1) << " and " << i;
    }
    EXPECT_EQ(result.back().end, static_cast<uint32_t>(runes.size()));
}

TEST(MPSegmentTest, AllSingleChars) {
    auto dict = DictTrie{DICT_FILE};
    auto runes = decode(std::string_view{"的了是"});
    auto result = MPSegment::cut(dict, runes);
    auto words = to_strings(runes, result);

    ASSERT_EQ(words.size(), 3);
    EXPECT_EQ(words[0], "的");
    EXPECT_EQ(words[1], "了");
    EXPECT_EQ(words[2], "是");
}
