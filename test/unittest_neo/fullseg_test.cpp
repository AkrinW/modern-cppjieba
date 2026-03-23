#include "../TestUtils.hpp"
#include "gtest/gtest.h"
#include "neo/DictTrie.hpp"
#include "neo/FullSegment.hpp"
#include "neo/Unicode.hpp"

#include "test_paths.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace neo_cppjieba;

inline constexpr auto DICT_FILE = std::string_view{DICT_DIR "/jieba.dict.utf8"};

TEST(FullSegmentTest, EmptyInput) {
    auto dict = DictTrie{DICT_FILE};
    auto runes = Unicode{};
    auto result = FullSegment::cut(dict, runes);
    EXPECT_TRUE(result.empty());
}

TEST(FullSegmentTest, SingleCharDictWord) {
    auto dict = DictTrie{DICT_FILE};
    auto runes = decode(std::string_view{"我"});
    auto result = FullSegment::cut(dict, runes);
    auto words = to_strings(runes, result);
    ASSERT_EQ(words.size(), 1);
    EXPECT_EQ(words[0], "我");
}

TEST(FullSegmentTest, ClassicSentence) {
    auto dict = DictTrie{DICT_FILE};
    auto runes = decode(std::string_view{"我来自北京邮电大学"});
    auto result = FullSegment::cut(dict, runes);
    auto words = to_strings(runes, result);

    auto expected =
        std::vector<std::string>{"我", "来自", "北京", "北京邮电", "北京邮电大学", "邮电", "邮电大学", "电大", "大学"};
    EXPECT_EQ(words, expected) << "actual: " << join(words);
}

TEST(FullSegmentTest, AnotherSentence) {
    auto dict = DictTrie{DICT_FILE};
    auto runes = decode(std::string_view{"他来到了网易杭研大厦"});
    auto result = FullSegment::cut(dict, runes);
    auto words = to_strings(runes, result);

    auto has = [&](const std::string &w) {
        return std::ranges::find(words, w) != words.end();
    };
    EXPECT_TRUE(has("来到")) << "actual: " << join(words);
    EXPECT_TRUE(has("大厦")) << "actual: " << join(words);
    EXPECT_TRUE(has("他")) << "actual: " << join(words);
}

TEST(FullSegmentTest, NanjingBridge) {
    auto dict = DictTrie{DICT_FILE};
    auto runes = decode(std::string_view{"南京市长江大桥"});
    auto result = FullSegment::cut(dict, runes);
    auto words = to_strings(runes, result);

    auto has = [&](const std::string &w) {
        return std::ranges::find(words, w) != words.end();
    };
    EXPECT_TRUE(has("南京")) << "actual: " << join(words);
    EXPECT_TRUE(has("南京市")) << "actual: " << join(words);
    EXPECT_TRUE(has("长江")) << "actual: " << join(words);
    EXPECT_TRUE(has("大桥")) << "actual: " << join(words);
    EXPECT_TRUE(has("长江大桥")) << "actual: " << join(words);
}

TEST(FullSegmentTest, SeparatorsArePreserved) {
    auto dict = DictTrie{DICT_FILE};
    auto runes = decode(std::string_view{"我来自北京，他来自上海"});
    auto result = FullSegment::cut(dict, runes);
    auto words = to_strings(runes, result);

    auto has = [&](const std::string &w) {
        return std::ranges::find(words, w) != words.end();
    };
    EXPECT_TRUE(has("来自")) << "actual: " << join(words);
    EXPECT_TRUE(has("北京")) << "actual: " << join(words);
    EXPECT_TRUE(has("上海")) << "actual: " << join(words);
    EXPECT_TRUE(has("，")) << "actual: " << join(words);
}

TEST(FullSegmentTest, WordRangeSlice) {
    auto dict = DictTrie{DICT_FILE};
    auto runes = decode(std::string_view{"北京大学"});
    auto result = FullSegment::cut(dict, runes);

    for (auto &&wr : result) {
        auto slice = wr.slice(runes);
        EXPECT_EQ(slice.size(), wr.size());
        auto word = encode(slice);
        EXPECT_FALSE(word.empty());
    }
}

TEST(FullSegmentTest, WordRangeEquality) {
    auto a = WordRange{0, 3};
    auto b = WordRange{0, 3};
    auto c = WordRange{1, 3};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(FullSegmentTest, AllSingleChars) {
    auto dict = DictTrie{DICT_FILE};
    auto runes = decode(std::string_view{"的了是"});
    auto result = FullSegment::cut(dict, runes);
    auto words = to_strings(runes, result);

    auto has = [&](const std::string &w) {
        return std::ranges::find(words, w) != words.end();
    };
    EXPECT_TRUE(has("的")) << "actual: " << join(words);
    EXPECT_TRUE(has("了")) << "actual: " << join(words);
    EXPECT_TRUE(has("是")) << "actual: " << join(words);
}

TEST(FullSegmentTest, LongSentence) {
    auto dict = DictTrie{DICT_FILE};
    auto input = std::string_view{"中华人民共和国中央人民政府今天成立了"};
    auto runes = decode(input);
    auto result = FullSegment::cut(dict, runes);
    auto words = to_strings(runes, result);

    EXPECT_GT(words.size(), 5u);

    auto has = [&](const std::string &w) {
        return std::ranges::find(words, w) != words.end();
    };
    EXPECT_TRUE(has("中华")) << "actual: " << join(words);
    EXPECT_TRUE(has("人民")) << "actual: " << join(words);
    EXPECT_TRUE(has("共和国")) << "actual: " << join(words);
    EXPECT_TRUE(has("中央")) << "actual: " << join(words);
    EXPECT_TRUE(has("政府")) << "actual: " << join(words);
    EXPECT_TRUE(has("成立")) << "actual: " << join(words);
    EXPECT_TRUE(has("今天")) << "actual: " << join(words);
}

TEST(FullSegmentTest, Deterministic) {
    auto dict = DictTrie{DICT_FILE};
    auto runes = decode(std::string_view{"我来自北京邮电大学"});

    auto result1 = FullSegment::cut(dict, runes);
    auto result2 = FullSegment::cut(dict, runes);

    ASSERT_EQ(result1.size(), result2.size());
    for (auto i = size_t{0}; i < result1.size(); ++i) {
        EXPECT_EQ(result1[i], result2[i]);
    }
}
