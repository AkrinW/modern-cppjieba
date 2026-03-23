#include "../QuerySegmentCompare.hpp"
#include "../TestUtils.hpp"
#include "gtest/gtest.h"
#include "neo/DictTrie.hpp"
#include "neo/HMModel.hpp"
#include "neo/QuerySegment.hpp"
#include "neo/Unicode.hpp"

#include "test_paths.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace neo_cppjieba;

inline constexpr auto DICT_FILE = std::string_view{DICT_DIR "/jieba.dict.utf8"};
inline constexpr auto HMM_MODEL_FILE = std::string_view{DICT_DIR "/hmm_model.utf8"};

TEST(QuerySegmentNeoTest, EmptyInput) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = Unicode{};
    auto result = QuerySegment<>::cut(dict, model, runes);
    EXPECT_TRUE(result.empty());
}

TEST(QuerySegmentNeoTest, SingleChar) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"我"});
    auto result = QuerySegment<>::cut(dict, model, runes);
    auto words = to_strings(runes, result);
    ASSERT_EQ(words.size(), 1);
    EXPECT_EQ(words[0], "我");
}

TEST(QuerySegmentNeoTest, SpanSubrangeUsesLocalOffsets) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"甲中国科学院乙"});
    auto span = std::span<const Rune>{runes}.subspan(1, runes.size() - 2);
    auto result = QuerySegment<>::cut(dict, model, span);
    auto words = to_strings(span, result);

    EXPECT_EQ(words, std::vector<std::string>({"中国", "科学", "学院", "科学院", "中国科学院"}));
    ASSERT_FALSE(result.empty());
    EXPECT_EQ(result.front().begin, 0u);
    EXPECT_EQ(result.back().end, static_cast<uint32_t>(span.size()));
}

TEST(QuerySegmentNeoTest, TwoCharWord) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"亲口交代"});
    auto result = QuerySegment<>::cut(dict, model, runes);
    auto words = to_strings(runes, result);
    EXPECT_EQ(join(words), "亲口/交代") << "actual: " << join(words);
}

TEST(QuerySegmentNeoTest, ClassicSentence) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"小明硕士毕业于中国科学院计算所，后在日本京都大学深造"});
    auto result = QuerySegment<>::cut(dict, model, runes);
    auto words = to_strings(runes, result);
    auto expected = std::string{
        "小明/硕士/毕业/于/中国/科学/学院/科学院/中国科学院/计算/计算所/，/后/在/日本/京都/大学/日本京都大学/深造"};
    EXPECT_EQ(join(words), expected) << "actual: " << join(words);
}

TEST(QuerySegmentNeoTest, SubWordExtraction) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"他心理健康"});
    auto result = QuerySegment<>::cut(dict, model, runes);
    auto words = to_strings(runes, result);
    EXPECT_EQ(join(words), "他/心理/健康/心理健康") << "actual: " << join(words);
}

TEST(QuerySegmentNeoTest, ChineseAcademy) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"中国科学院"});
    auto result = QuerySegment<>::cut(dict, model, runes);
    auto words = to_strings(runes, result);
    EXPECT_EQ(join(words), "中国/科学/学院/科学院/中国科学院") << "actual: " << join(words);
}

TEST(QuerySegmentNeoTest, UnicodeOverloadWithSeparators) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto sentence = std::string_view{"中国科学院，后在日本京都大学深造"};
    auto runes = decode(sentence);
    auto result = QuerySegment<>::cut(dict, model, runes);
    auto words = to_strings(runes, result);
    auto actual = join(words);
    EXPECT_NE(actual.find("中国科学院"), std::string::npos) << "actual: " << actual;
    EXPECT_NE(actual.find("，"), std::string::npos) << "actual: " << actual;
    EXPECT_NE(actual.find("京都"), std::string::npos) << "actual: " << actual;
}

TEST(QuerySegmentNeoTest, NoHmm) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"中国科学院"});
    auto result_hmm = QuerySegment<>::cut(dict, model, runes);
    auto result_no = QuerySegment<false>::cut(dict, model, runes);

    EXPECT_EQ(to_strings(runes, result_hmm), to_strings(runes, result_no));
}

TEST(QuerySegmentNeoTest, ReuseDagMatchesRequery) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"小明硕士毕业于中国科学院计算所，后在日本京都大学深造"});

    auto result_requery = test::query_cut_requery(dict, model, runes);
    auto result_buffered = test::query_cut_buffered_dag(dict, model, runes);
    auto result_inline = QuerySegment<>::cut(dict, model, runes);

    EXPECT_EQ(result_buffered, result_requery);
    EXPECT_EQ(result_inline, result_requery);
    EXPECT_EQ(to_strings(runes, result_buffered), to_strings(runes, result_requery));
    EXPECT_EQ(to_strings(runes, result_inline), to_strings(runes, result_requery));
}

TEST(QuerySegmentNeoTest, ReuseDagMatchesRequeryWithoutHmm) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"他来到了网易杭研大厦"});

    auto result_requery = test::query_cut_requery<false>(dict, model, runes);
    auto result_buffered = test::query_cut_buffered_dag<false>(dict, model, runes);
    auto result_inline = QuerySegment<false>::cut(dict, model, runes);

    EXPECT_EQ(result_buffered, result_requery);
    EXPECT_EQ(result_inline, result_requery);
    EXPECT_EQ(to_strings(runes, result_buffered), to_strings(runes, result_requery));
    EXPECT_EQ(to_strings(runes, result_inline), to_strings(runes, result_requery));
}
