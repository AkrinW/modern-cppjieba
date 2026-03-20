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

// ─── Basic segmentation ─────────────────────────────────────────────────────

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
    // The canonical CutAll test case from jieba
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

    // Full mode: all sub-words present in the dictionary
    // "他" "来到" "了" "网易" "杭" "研" "大厦"
    // Note: "杭研" is NOT in the standard dictionary, so "杭" and "研" are emitted separately
    // Verify at least the key words are present
    EXPECT_FALSE(words.empty());

    // Check that "来到" and "大厦" are in the results
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

    // Full mode should emit all overlapping words:
    // 南京, 南京市, 市长, 长江, 长江大桥, 大桥, etc.
    auto has = [&](const std::string &w) {
        return std::ranges::find(words, w) != words.end();
    };
    EXPECT_TRUE(has("南京")) << "actual: " << join(words);
    EXPECT_TRUE(has("南京市")) << "actual: " << join(words);
    EXPECT_TRUE(has("长江")) << "actual: " << join(words);
    EXPECT_TRUE(has("大桥")) << "actual: " << join(words);
    EXPECT_TRUE(has("长江大桥")) << "actual: " << join(words);
}

// ─── WordRange interface ────────────────────────────────────────────────────

TEST(FullSegmentTest, WordRangeSlice) {
    auto dict = DictTrie{DICT_FILE};
    auto runes = decode(std::string_view{"北京大学"});
    auto result = FullSegment::cut(dict, runes);

    // Each WordRange::slice should give back the correct rune sub-span
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

// ─── Edge cases ─────────────────────────────────────────────────────────────

TEST(FullSegmentTest, AllSingleChars) {
    // Characters that are each a single-char dict word with no multi-char combinations
    auto dict = DictTrie{DICT_FILE};
    auto runes = decode(std::string_view{"的了是"});
    auto result = FullSegment::cut(dict, runes);
    auto words = to_strings(runes, result);

    // Each should appear as its own word since they don't form multi-char combinations
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

// ─── Determinism ─────────────────────────────────────────────────────────────

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

// ─── String overload with pre_filter_view ───────────────────────────────────

TEST(FullSegmentTest, StringOverloadBasic) {
    auto dict = DictTrie{DICT_FILE};
    auto words = FullSegment::cut(dict, std::string_view{"我来自北京邮电大学"});

    auto expected =
        std::vector<std::string>{"我", "来自", "北京", "北京邮电", "北京邮电大学", "邮电", "邮电大学", "电大", "大学"};
    EXPECT_EQ(words, expected) << "actual: " << join(words);
}

TEST(FullSegmentTest, StringOverloadUtf16) {
    auto dict = DictTrie{DICT_FILE};
    auto words = FullSegment::cut(dict, std::u16string_view{u"我来自北京邮电大学"});

    auto expected =
        std::vector<std::u16string>{u"我", u"来自", u"北京", u"北京邮电", u"北京邮电大学", u"邮电", u"邮电大学", u"电大", u"大学"};
    EXPECT_EQ(words, expected);
}

TEST(FullSegmentTest, StringOverloadEmpty) {
    auto dict = DictTrie{DICT_FILE};
    auto words = FullSegment::cut(dict, std::string_view{""});
    EXPECT_TRUE(words.empty());
}

TEST(FullSegmentTest, StringOverloadWithSeparators) {
    // Sentence with space and Chinese comma as separators
    auto dict = DictTrie{DICT_FILE};
    auto words = FullSegment::cut(dict, std::string_view{"我来自北京，他来自上海"});

    auto has = [&](const std::string &w) {
        return std::ranges::find(words, w) != words.end();
    };
    // Non-separator words should be present
    EXPECT_TRUE(has("来自")) << "actual: " << join(words);
    EXPECT_TRUE(has("北京")) << "actual: " << join(words);
    EXPECT_TRUE(has("上海")) << "actual: " << join(words);
    // The Chinese comma separator should be emitted as its own token
    EXPECT_TRUE(has("，")) << "actual: " << join(words);
}

TEST(FullSegmentTest, StringOverloadMultipleSeparators) {
    // Multiple consecutive separators should each be emitted individually
    auto dict = DictTrie{DICT_FILE};
    auto words = FullSegment::cut(dict, std::string_view{"北京 　上海"});

    auto has = [&](const std::string &w) {
        return std::ranges::find(words, w) != words.end();
    };
    EXPECT_TRUE(has("北京")) << "actual: " << join(words);
    EXPECT_TRUE(has("上海")) << "actual: " << join(words);
    EXPECT_TRUE(has(" ")) << "should emit space separator, actual: " << join(words);
}

TEST(FullSegmentTest, StringOverloadMatchesRuneOverload) {
    // Verify that the string overload (with pre_filter_view) on a sentence
    // WITHOUT separators produces the same tokens as the rune overload.
    auto dict = DictTrie{DICT_FILE};
    auto sentence = std::string_view{"南京市长江大桥"};

    // Rune-based overload
    auto runes = decode(sentence);
    auto rune_result = FullSegment::cut(dict, runes);
    auto rune_words = to_strings(runes, rune_result);

    // String-based overload
    auto string_words = FullSegment::cut(dict, sentence);

    EXPECT_EQ(rune_words, string_words) << "rune: " << join(rune_words) << "\nstring: " << join(string_words);
}

TEST(FullSegmentTest, StringOverloadLongWithSeparators) {
    auto dict = DictTrie{DICT_FILE};
    auto words = FullSegment::cut(dict, std::string_view{"中华人民共和国中央人民政府今天成立了。伟大的祖国"});

    auto has = [&](const std::string &w) {
        return std::ranges::find(words, w) != words.end();
    };

    // Words from first clause
    EXPECT_TRUE(has("中华")) << "actual: " << join(words);
    EXPECT_TRUE(has("人民")) << "actual: " << join(words);
    EXPECT_TRUE(has("共和国")) << "actual: " << join(words);
    EXPECT_TRUE(has("成立")) << "actual: " << join(words);
    // Separator (Chinese period)
    EXPECT_TRUE(has("。")) << "actual: " << join(words);
    // Words from second clause
    EXPECT_TRUE(has("伟大")) << "actual: " << join(words);
    EXPECT_TRUE(has("祖国")) << "actual: " << join(words);
}
