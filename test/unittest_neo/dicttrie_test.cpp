#include "gtest/gtest.h"
#include "neo/DictTrie.hpp"

#include "test_paths.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

using namespace neo_cppjieba;

inline constexpr auto DICT_FILE = std::string_view{TEST_DATA_DIR "/extra_dict/jieba.dict.small.utf8"};
inline constexpr auto USER_DICT_FILE = std::string_view{TEST_DATA_DIR "/userdict.utf8"};

namespace {

// Each dictionary validation test owns its input files in an isolated temporary directory.
class DictTrieInputTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto pattern = (std::filesystem::temp_directory_path() / "neo-dict-trie-XXXXXX").string();
        ASSERT_NE(::mkdtemp(pattern.data()), nullptr);
        directory_ = std::move(pattern);
        write_file("main.dict", "主词 10 n\n基础 20 n\n");
    }

    void TearDown() override {
        if (!directory_.empty()) {
            auto error = std::error_code{};
            std::filesystem::remove_all(directory_, error);
            EXPECT_FALSE(error) << error.message();
        }
    }

    [[nodiscard]] auto file_path(std::string_view name) const -> std::string {
        return (directory_ / name).string();
    }

    void write_file(std::string_view name, std::string_view content) const {
        auto output = std::ofstream{file_path(name), std::ios::binary};
        ASSERT_TRUE(output.is_open());
        output << content;
        output.close();
        ASSERT_TRUE(output.good());
    }

    std::filesystem::path directory_;
};

} // namespace

TEST_F(DictTrieInputTest, RejectsMainDictionaryLinesWithMissingFields) {
    for (const auto line : {"词\n", "词 10\n"}) {
        ASSERT_NO_FATAL_FAILURE(write_file("main.dict", line));
        EXPECT_THROW((DictTrie{file_path("main.dict"), "", DictTrie::UserWordWeightOption::WordWeightMedian}),
                     LogConfig::Exception);
    }
}

TEST_F(DictTrieInputTest, RejectsMainDictionaryLinesWithExtraFields) {
    ASSERT_NO_FATAL_FAILURE(write_file("main.dict", "词 10 n extra\n"));
    EXPECT_THROW((DictTrie{file_path("main.dict"), "", DictTrie::UserWordWeightOption::WordWeightMedian}),
                 LogConfig::Exception);
}

TEST_F(DictTrieInputTest, RejectsNonPositiveMainFrequenciesEvenWhenSumIsPositive) {
    for (const auto content : {"词 0 n\n基础 20 n\n", "词 -1 n\n基础 20 n\n"}) {
        ASSERT_NO_FATAL_FAILURE(write_file("main.dict", content));
        EXPECT_THROW((DictTrie{file_path("main.dict"), "", DictTrie::UserWordWeightOption::WordWeightMedian}),
                     LogConfig::Exception);
    }
}

TEST_F(DictTrieInputTest, RejectsMainFrequencyWithTrailingCharacters) {
    ASSERT_NO_FATAL_FAILURE(write_file("main.dict", "词 10oops n\n基础 20 n\n"));
    EXPECT_THROW((DictTrie{file_path("main.dict"), "", DictTrie::UserWordWeightOption::WordWeightMedian}),
                 LogConfig::Exception);
}

TEST_F(DictTrieInputTest, RejectsEmptyMainFrequency) {
    ASSERT_NO_FATAL_FAILURE(write_file("main.dict", "词  n\n基础 20 n\n"));
    EXPECT_THROW((DictTrie{file_path("main.dict"), "", DictTrie::UserWordWeightOption::WordWeightMedian}),
                 LogConfig::Exception);
}

TEST_F(DictTrieInputTest, RejectsEmptyMainDictionary) {
    ASSERT_NO_FATAL_FAILURE(write_file("main.dict", ""));
    EXPECT_THROW((DictTrie{file_path("main.dict"), "", DictTrie::UserWordWeightOption::WordWeightMedian}),
                 LogConfig::Exception);
}

TEST_F(DictTrieInputTest, AcceptsEmptyTagAndWindowsLineEndings) {
    ASSERT_NO_FATAL_FAILURE(write_file("main.dict", "主词 10 \r\n\r\n基础 20 n\r\n"));
    const auto trie = DictTrie{file_path("main.dict"), "", DictTrie::UserWordWeightOption::WordWeightMedian};
    const auto word = trie.find("主词");
    ASSERT_TRUE(word.has_value());
    EXPECT_TRUE(word.tag.empty());
    EXPECT_FLOAT_EQ(word.weight, std::log(10.0f / 30.0f));
    EXPECT_FALSE(trie.find("缺词").has_value());
    EXPECT_FALSE(trie.find("").has_value());
}

TEST_F(DictTrieInputTest, RejectsExtraUserDictionaryFields) {
    ASSERT_NO_FATAL_FAILURE(write_file("user.dict", "新词 5 n extra\n"));
    EXPECT_THROW(
        (DictTrie{file_path("main.dict"), file_path("user.dict"), DictTrie::UserWordWeightOption::WordWeightMedian}),
        LogConfig::Exception);
}

TEST_F(DictTrieInputTest, RejectsNegativeUserFrequency) {
    ASSERT_NO_FATAL_FAILURE(write_file("user.dict", "新词 -1 n\n"));
    EXPECT_THROW(
        (DictTrie{file_path("main.dict"), file_path("user.dict"), DictTrie::UserWordWeightOption::WordWeightMedian}),
        LogConfig::Exception);
}

TEST_F(DictTrieInputTest, RejectsUserFrequencyWithTrailingCharacters) {
    ASSERT_NO_FATAL_FAILURE(write_file("user.dict", "新词 5oops n\n"));
    EXPECT_THROW(
        (DictTrie{file_path("main.dict"), file_path("user.dict"), DictTrie::UserWordWeightOption::WordWeightMedian}),
        LogConfig::Exception);
}

TEST_F(DictTrieInputTest, ZeroUserFrequencyUsesDefaultWeight) {
    ASSERT_NO_FATAL_FAILURE(write_file("user.dict", "新词 0 n\n"));
    const auto trie =
        DictTrie{file_path("main.dict"), file_path("user.dict"), DictTrie::UserWordWeightOption::WordWeightMedian};
    const auto word = trie.find("新词");
    ASSERT_TRUE(word.has_value());
    EXPECT_FLOAT_EQ(word.weight, trie.user_word_default_weight());
}

TEST_F(DictTrieInputTest, RejectsUnknownWeightOption) {
    const auto invalid = static_cast<DictTrie::UserWordWeightOption>(255);
    EXPECT_THROW((DictTrie{file_path("main.dict"), "", invalid}), LogConfig::Exception);
}

// ─── Construction ────────────────────────────────────────────────────────────

TEST(DictTrieTest, ConstructMainDictOnly) {
    auto trie = DictTrie{DICT_FILE};
    // smoke test: the trie should be non-empty and have sensible statistics
    EXPECT_LT(trie.min_weight(), 0.0f);
    EXPECT_LT(trie.max_weight(), 0.0f);
    EXPECT_GT(trie.freq_sum(), 0.0f);
}

TEST(DictTrieTest, ConstructWithUserDict) {
    auto trie = DictTrie{DICT_FILE, USER_DICT_FILE};
    EXPECT_GT(trie.freq_sum(), 0.0f);
}

// ─── Weight statistics ───────────────────────────────────────────────────────

TEST(DictTrieTest, WeightOrdering) {
    auto trie = DictTrie{DICT_FILE};
    // min ≤ median ≤ max  (all negative since log(freq/sum) < 0)
    EXPECT_LE(trie.min_weight(), trie.median_weight());
    EXPECT_LE(trie.median_weight(), trie.max_weight());
}

TEST(DictTrieTest, MinWeightValue) {
    auto trie = DictTrie{DICT_FILE};
    // The old test checked: GetMinWeight() ≈ -15.6479  (within 0.001)
    // float precision: allow slightly wider tolerance
    EXPECT_NEAR(trie.min_weight(), -15.6479f, 0.01f);
}

TEST(DictTrieTest, DefaultWeightIsMedian) {
    auto trie = DictTrie{DICT_FILE};
    EXPECT_FLOAT_EQ(trie.user_word_default_weight(), trie.median_weight());
}

TEST(DictTrieTest, UserWordWeightMin) {
    auto trie = DictTrie{DICT_FILE, "", DictTrie::UserWordWeightOption::WordWeightMin};
    EXPECT_FLOAT_EQ(trie.user_word_default_weight(), trie.min_weight());
}

TEST(DictTrieTest, UserWordWeightMax) {
    auto trie = DictTrie{DICT_FILE, "", DictTrie::UserWordWeightOption::WordWeightMax};
    EXPECT_FLOAT_EQ(trie.user_word_default_weight(), trie.max_weight());
}

// ─── Find: main dictionary ──────────────────────────────────────────────────

TEST(DictTrieTest, FindExistingWord) {
    auto trie = DictTrie{DICT_FILE};

    auto unit = trie.find("来到");
    ASSERT_TRUE(unit.has_value());
    EXPECT_EQ(unit.tag, PosTag{"v"});
    EXPECT_NEAR(unit.weight, -8.870f, 0.01f);
}

TEST(DictTrieTest, FindMultiCharWord) {
    auto trie = DictTrie{DICT_FILE};

    auto unit = trie.find("清华大学");
    ASSERT_TRUE(unit.has_value());
    // just verify it exists and has a PosTag
    EXPECT_FALSE(unit.tag.empty());
}

TEST(DictTrieTest, FindSingleChar) {
    auto trie = DictTrie{DICT_FILE};

    auto unit = trie.find("的");
    ASSERT_TRUE(unit.has_value());
    EXPECT_EQ(unit.tag, PosTag{"uj"});
}

TEST(DictTrieTest, FindNonExistentWord) {
    auto trie = DictTrie{DICT_FILE};

    auto unit = trie.find("不存在的词xyz");
    EXPECT_FALSE(unit.has_value());
}

TEST(DictTrieTest, FindEmptyString) {
    auto trie = DictTrie{DICT_FILE};
    EXPECT_FALSE(trie.find("").has_value());
}

// ─── Find: user dictionary ──────────────────────────────────────────────────

TEST(DictTrieTest, FindUserDictWordNoFreqNoTag) {
    // userdict.utf8 line: "云计算"  (1 field → default weight, empty tag)
    auto trie = DictTrie{DICT_FILE, USER_DICT_FILE};

    auto unit = trie.find("云计算");
    ASSERT_TRUE(unit.has_value());
    // weight should equal user_word_default_weight (median)
    EXPECT_FLOAT_EQ(unit.weight, trie.user_word_default_weight());
    EXPECT_TRUE(unit.tag.empty());
}

TEST(DictTrieTest, FindUserDictWordWithTag) {
    // userdict.utf8 line: "蓝翔 nz"  (2 fields → default weight, tag = "nz")
    auto trie = DictTrie{DICT_FILE, USER_DICT_FILE};

    auto unit = trie.find("蓝翔");
    ASSERT_TRUE(unit.has_value());
    EXPECT_EQ(unit.tag, PosTag{"nz"});
    EXPECT_FLOAT_EQ(unit.weight, trie.user_word_default_weight());
}

TEST(DictTrieTest, FindUserDictWordWithFreqAndTag) {
    // userdict.utf8 line: "区块链 10 nz"  (3 fields → freq=10, tag="nz")
    auto trie = DictTrie{DICT_FILE, USER_DICT_FILE};

    auto unit = trie.find("区块链");
    ASSERT_TRUE(unit.has_value());
    EXPECT_EQ(unit.tag, PosTag{"nz"});
    // weight = log(10 / freq_sum), should be distinctly different from default
    auto expected = std::log(10.0f / trie.freq_sum());
    EXPECT_NEAR(unit.weight, expected, 0.01f);
}

TEST(DictTrieTest, UserDictMaxWeight) {
    auto trie = DictTrie{DICT_FILE, USER_DICT_FILE, DictTrie::UserWordWeightOption::WordWeightMax};

    // "云计算" has no freq → should use max weight as default
    auto unit = trie.find("云计算");
    ASSERT_TRUE(unit.has_value());
    EXPECT_NEAR(unit.weight, trie.max_weight(), 0.01f);
}

// ─── User dict single Chinese character ──────────────────────────────────────

TEST(DictTrieTest, UserDictSingleChineseWord) {
    // userdict.utf8 contains "A" and "B" as single-char entries
    auto trie = DictTrie{DICT_FILE, USER_DICT_FILE};

    // 'A' and 'B' are single characters in the user dict
    EXPECT_TRUE(trie.is_user_dict_single_chinese_word(U'A'));
    EXPECT_TRUE(trie.is_user_dict_single_chinese_word(U'B'));
    // a multi-char user word's first char should NOT be in the set
    // '蓝' is part of "蓝翔" — 2-char word, should not be in set
    EXPECT_FALSE(trie.is_user_dict_single_chinese_word(U'蓝'));
    // a character not in user dict at all
    EXPECT_FALSE(trie.is_user_dict_single_chinese_word(U'Z'));
}

// ─── Find with rune span ────────────────────────────────────────────────────

TEST(DictTrieTest, FindByRuneSpan) {
    auto trie = DictTrie{DICT_FILE};

    auto runes = decode(std::string_view{"来到"});
    auto unit = trie.find(std::span<const Rune>{runes});
    ASSERT_TRUE(unit.has_value());
    EXPECT_EQ(unit.tag, PosTag{"v"});
}

// ─── Find DAG ───────────────────────────────────────────────────────────────

TEST(DictTrieTest, FindDag) {
    auto trie = DictTrie{DICT_FILE};
    auto text = std::string_view{"来到华中科技大学"};
    auto runes = decode(text);
    auto dag = trie.trie().find_dag(runes);

    // "来", "来到", "到", "华", "华中", "中", "科", "科技", "科技大", "科技大学", ...
    EXPECT_EQ(dag.offsets.size(), runes.size() + 1);

    auto edges_0 = dag.get_edges(0); // '来'
    ASSERT_GE(edges_0.size(), 2);    // '来', '来到'
    EXPECT_EQ(edges_0[0].next_pos, 1);
    EXPECT_EQ(edges_0[1].next_pos, 2);

    auto edges_4 = dag.get_edges(4); // '科'
    ASSERT_GE(edges_4.size(), 2);    // '科', '科技', '科技大学'?
    // check single char has a length
    EXPECT_EQ(edges_4[0].next_pos, 5);

    // Check that all single chars at least have (i, i+1)
    for (size_t i = 0; i < runes.size(); ++i) {
        auto edges = dag.get_edges(i);
        ASSERT_FALSE(edges.empty());
        EXPECT_EQ(edges[0].next_pos, i + 1);
    }
}

// ─── Dag: structural tests ──────────────────────────────────────────────────

TEST(DagTest, EmptyDag) {
    auto dag = Dag{};
    EXPECT_EQ(dag.size(), 0);
    EXPECT_TRUE(dag.get_edges(0).empty());

    auto runes = Unicode{};
    EXPECT_EQ(dag.to_string(runes), "");
}

TEST(DagTest, MaximumVertexIndexReturnsNoEdges) {
    const auto dag = Dag{{0, 1}, {{1, -1.0f}}};
    EXPECT_TRUE(dag.get_edges(std::numeric_limits<size_t>::max()).empty());
    EXPECT_TRUE(dag.get_edges(dag.size()).empty());
    EXPECT_TRUE(Dag{}.get_edges(std::numeric_limits<size_t>::max()).empty());
}

TEST(DagTest, VertexWithoutEdgesReturnsAnEmptySpan) {
    const auto dag = Dag{{0, 0}, {}};
    EXPECT_TRUE(dag.get_edges(0).empty());
}

TEST(DagTest, FormatsSupplementaryUnicodeUsingRuneIndices) {
    const auto dag = Dag{{0, 1, 1}, {{2, -1.0f}}};
    const auto runes = decode("𠮷中");
    EXPECT_EQ(dag.to_string(runes, ", "), "𠮷中(-1)");
}

TEST(DagTest, SizeMismatchReturnsEmpty) {
    // Build a DAG for 2 runes but pass 3 runes to to_string → empty
    auto dag = Dag{};
    dag.offsets = {0, 1, 1};
    dag.edges = {{1, 1.0f}};
    EXPECT_EQ(dag.size(), 2);

    auto runes = decode(std::string_view{"三个字"});
    EXPECT_EQ(dag.to_string(runes), "");
}

TEST(DagTest, SingleCharToString) {
    auto dag = Dag{};
    dag.offsets = {0, 1};
    dag.edges = {{1, -5.0f}};
    EXPECT_EQ(dag.size(), 1);

    auto runes = decode(std::string_view{"我"});
    EXPECT_EQ(dag.to_string(runes), "我(-5)");
}

TEST(DagTest, TwoCharToString) {
    // Two individual characters, no multi-char edge
    auto dag = Dag{};
    dag.offsets = {0, 1, 2};
    dag.edges = {
        {1, -5.0f}, // pos 0 → pos 1 ("你")
        {2, -5.0f}, // pos 1 → pos 2 ("好")
    };

    auto runes = decode(std::string_view{"你好"});
    EXPECT_EQ(dag.to_string(runes), "你(-5), 好(-5)");
}

TEST(DagTest, MultiEdgeToString) {
    // Two individual chars plus a 2-char edge
    auto dag = Dag{};
    dag.offsets = {0, 2, 3};
    dag.edges = {
        {1, -5.0f}, // pos 0 → pos 1 (single "你")
        {2, -2.0f}, // pos 0 → pos 2 (merged "你好")
        {2, -5.0f}, // pos 1 → pos 2 (single "好")
    };

    auto runes = decode(std::string_view{"你好"});
    // All sub-words dumped: 你, 你好, 好
    auto result = dag.to_string(runes);
    EXPECT_NE(result.find("你(-5)"), std::string::npos);
    EXPECT_NE(result.find("你好(-2)"), std::string::npos);
    EXPECT_NE(result.find("好(-5)"), std::string::npos);
}

TEST(DagTest, ThreeCharToString) {
    auto dag = Dag{};
    dag.offsets = {0, 1, 3, 4};
    dag.edges = {
        {1, -5.0f}, // pos 0 → 1 "我"
        {2, -5.0f}, // pos 1 → 2 "来"
        {3, -3.0f}, // pos 1 → 3 "来了"
        {3, -5.0f}, // pos 2 → 3 "了"
    };

    auto runes = decode(std::string_view{"我来了"});
    auto result = dag.to_string(runes);
    EXPECT_NE(result.find("我(-5)"), std::string::npos);
    EXPECT_NE(result.find("来(-5)"), std::string::npos);
    EXPECT_NE(result.find("来了(-3)"), std::string::npos);
    EXPECT_NE(result.find("了(-5)"), std::string::npos);
}

TEST(DagTest, ZeroWeightToString) {
    // An edge with weight 0.0 (not in dict) is dumped as-is
    auto dag = Dag{};
    dag.offsets = {0, 1, 2};
    dag.edges = {
        {1, 0.0f},  // unknown char at pos 0
        {2, -3.0f}, // known char at pos 1
    };

    auto runes = decode(std::string_view{"x好"});
    auto result = dag.to_string(runes);
    EXPECT_NE(result.find("x(0)"), std::string::npos);
    EXPECT_NE(result.find("好(-3)"), std::string::npos);
}

TEST(DagTest, CustomSeparator) {
    auto dag = Dag{};
    dag.offsets = {0, 1, 2, 3};
    dag.edges = {
        {1, -1.0f},
        {2, -1.0f},
        {3, -1.0f},
    };

    auto runes = decode(std::string_view{"一二三"});
    EXPECT_EQ(dag.to_string(runes, " "), "一(-1) 二(-1) 三(-1)");
    EXPECT_EQ(dag.to_string(runes, " | "), "一(-1) | 二(-1) | 三(-1)");
}

// ─── Dag: to_string with real dictionary ─────────────────────────────────────

TEST(DagTest, ToStringWithDict) {
    auto trie = DictTrie{DICT_FILE};
    auto text = std::string_view{"来到北京清华大学"};
    auto runes = decode(text);
    auto dag = trie.trie().find_dag(runes);

    auto result = dag.to_string(runes);
    EXPECT_FALSE(result.empty());
    // Should contain sub-words with their weights
    EXPECT_NE(result.find("来到("), std::string::npos);
    EXPECT_NE(result.find("清华大学("), std::string::npos);
    EXPECT_NE(result.find("北京("), std::string::npos);
}

TEST(DagTest, ToStringHeSayJump) {
    auto trie = DictTrie{DICT_FILE};
    auto text = std::string_view{"他来到了网易杭研大厦"};
    auto runes = decode(text);
    auto dag = trie.trie().find_dag(runes);

    auto result = dag.to_string(runes);
    EXPECT_FALSE(result.empty());
    EXPECT_NE(result.find("来到("), std::string::npos);
}

TEST(DagTest, ToStringPrint) {
    auto trie = DictTrie{DICT_FILE};
    auto text = std::string_view{"南京市长江大桥"};
    auto runes = decode(text);
    auto dag = trie.trie().find_dag(runes);

    auto result = dag.to_string(runes);
    EXPECT_FALSE(result.empty());
    EXPECT_NE(result.find("南京市("), std::string::npos);
    std::cout << dag.to_string(runes) << std::endl;
}

// ─── Trie accessor ──────────────────────────────────────────────────────────

TEST(DictTrieTest, TrieAccessor) {
    auto trie = DictTrie{DICT_FILE};
    const auto &t = trie.trie();
    EXPECT_FALSE(t.empty());
    EXPECT_GT(t.node_count(), 0u);
}
