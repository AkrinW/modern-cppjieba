#include "../TestUtils.hpp"
#include "gtest/gtest.h"
#include "neo/DictTrie.hpp"
#include "neo/MPSegment.hpp"
#include "neo/Unicode.hpp"

#include "test_paths.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace neo_cppjieba;

inline constexpr auto DICT_FILE = std::string_view{DICT_DIR "/jieba.dict.utf8"};

// ─── Basic segmentation ─────────────────────────────────────────────────────

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
    // The canonical MP test case from jieba
    auto dict = DictTrie{DICT_FILE};
    auto sentence = std::string_view{"我来自北京邮电大学"};
    auto runes = decode(sentence);
    auto result = MPSegment::cut(dict, runes);
    auto words = to_strings(runes, result);

    // MP mode picks the maximum probability path:
    // "我" / "来自" / "北京邮电大学"
    auto expected = std::vector<std::string>{"我", "来自", "北京邮电大学"};
    EXPECT_EQ(words, expected) << "actual: " << join(words);
}

TEST(MPSegmentTest, NanjingBridge) {
    auto dict = DictTrie{DICT_FILE};
    auto runes = decode(std::string_view{"南京市长江大桥"});
    auto result = MPSegment::cut(dict, runes);
    auto words = to_strings(runes, result);

    // MP mode: "南京市" / "长江大桥"
    auto expected = std::vector<std::string>{"南京市", "长江大桥"};
    EXPECT_EQ(words, expected) << "actual: " << join(words);
}

TEST(MPSegmentTest, HunanChangsha) {
    auto dict = DictTrie{DICT_FILE};
    auto runes = decode(std::string_view{"湖南长沙市天心区"});
    auto result = MPSegment::cut(dict, runes);
    auto words = to_strings(runes, result);
    auto s = join(words);

    EXPECT_EQ(s, "湖南长沙市/天心区") << "actual: " << s;
}

// ─── String overload with pre-filter ─────────────────────────────────────────

TEST(MPSegmentTest, UnicodeOverloadWithSeparators) {
    auto dict = DictTrie{DICT_FILE};
    auto sentence = std::string_view{"我来自北京邮电大学。"};
    auto runes = decode(sentence);
    auto ranges = MPSegment::cut(dict, runes);
    auto words = to_strings(runes, ranges);
    auto expected = std::vector<std::string>{"我", "来自", "北京邮电大学", "。"};
    EXPECT_EQ(words, expected) << "actual: " << join(words);
}

TEST(MPSegmentTest, StringOverloadWithPunctuation) {
    auto dict = DictTrie{DICT_FILE};
    auto words = MPSegment::cut(dict, std::string_view{"我来自北京邮电大学。"});
    auto s = join(words);

    // Punctuation is emitted as separate token by pre_filter
    EXPECT_EQ(s, "我/来自/北京邮电大学/。") << "actual: " << s;
}

TEST(MPSegmentTest, StringOverloadUtf16) {
    auto dict = DictTrie{DICT_FILE};
    auto words = MPSegment::cut(dict, std::u16string_view{u"我来自北京邮电大学"});

    auto expected = std::vector<std::u16string>{u"我", u"来自", u"北京邮电大学"};
    EXPECT_EQ(words, expected);
}

TEST(MPSegmentTest, StringOverloadMixed) {
    auto dict = DictTrie{DICT_FILE};
    auto words = MPSegment::cut(dict, std::string_view{"B超 T恤"});
    auto s = join(words);

    EXPECT_EQ(s, "B超/ /T恤") << "actual: " << s;
}

TEST(MPSegmentTest, Unicode32Emoji) {
    auto dict = DictTrie{DICT_FILE};
    auto words = MPSegment::cut(dict, std::string_view{"天气很好，🙋 我们去郊游。"});
    auto s = join(words);

    EXPECT_EQ(s, "天气/很/好/，/🙋/ /我们/去/郊游/。") << "actual: " << s;
}

// ─── WordRange interface ────────────────────────────────────────────────────

TEST(MPSegmentTest, WordRangeContiguous) {
    // The MP path must be a contiguous partition of [0, n)
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
    // Characters that individually are in the dictionary but don't form longer words together
    auto dict = DictTrie{DICT_FILE};
    auto runes = decode(std::string_view{"的了是"});
    auto result = MPSegment::cut(dict, runes);
    auto words = to_strings(runes, result);

    // Each should be its own token
    ASSERT_EQ(words.size(), 3);
    EXPECT_EQ(words[0], "的");
    EXPECT_EQ(words[1], "了");
    EXPECT_EQ(words[2], "是");
}
