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

// ─── Basic segmentation ─────────────────────────────────────────────────────

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

TEST(QuerySegmentNeoTest, TwoCharWord) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"亲口交代"});
    auto result = QuerySegment<>::cut(dict, model, runes);
    auto words = to_strings(runes, result);
    auto actual = join(words);
    EXPECT_EQ(actual, "亲口/交代") << "actual: " << actual;
}

TEST(QuerySegmentNeoTest, ClassicSentence) {
    // Canonical query-mode test: "小明硕士毕业于中国科学院计算所，后在日本京都大学深造"
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto sentence = std::string_view{"小明硕士毕业于中国科学院计算所，后在日本京都大学深造"};

    auto result = QuerySegment<>::cut(dict, model, sentence);
    auto actual = join(result);
    auto expected = std::string{
        "小明/硕士/毕业/于/中国/科学/学院/科学院/中国科学院/计算/计算所/，/后/在/日本/京都/大学/日本京都大学/深造"};
    EXPECT_EQ(actual, expected) << "actual: " << actual;
}

TEST(QuerySegmentNeoTest, SubWordExtraction) {
    // "他心理健康" → "他/心理/健康/心理健康"
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto sentence = std::string_view{"他心理健康"};

    auto result = QuerySegment<>::cut(dict, model, sentence);
    auto actual = join(result);
    EXPECT_EQ(actual, "他/心理/健康/心理健康") << "actual: " << actual;
}

TEST(QuerySegmentNeoTest, ChineseAcademy) {
    // "中国科学院" should produce fine-grained sub-words
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto sentence = std::string_view{"中国科学院"};

    auto result = QuerySegment<>::cut(dict, model, sentence);
    auto actual = join(result);
    auto expected = std::string{"中国/科学/学院/科学院/中国科学院"};
    EXPECT_EQ(actual, expected) << "actual: " << actual;
}

// ─── String overload ─────────────────────────────────────────────────────────

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

TEST(QuerySegmentNeoTest, StringOverloadBasic) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto result = QuerySegment<>::cut(dict, model, std::string_view{"我来自北京邮电大学"});
    auto actual = join(result);
    // Query mode should produce sub-words for "北京邮电大学"
    EXPECT_FALSE(result.empty());
    // The result should contain "北京" and "大学" as sub-words
    EXPECT_NE(actual.find("北京"), std::string::npos) << "actual: " << actual;
    EXPECT_NE(actual.find("大学"), std::string::npos) << "actual: " << actual;
}

TEST(QuerySegmentNeoTest, StringOverloadUtf16) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto result = QuerySegment<>::cut(dict, model, std::u16string_view{u"中国科学院"});

    auto expected = std::vector<std::u16string>{u"中国", u"科学", u"学院", u"科学院", u"中国科学院"};
    EXPECT_EQ(result, expected);
}

TEST(QuerySegmentNeoTest, EmptyString) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto result = QuerySegment<>::cut(dict, model, std::string_view{""});
    EXPECT_TRUE(result.empty());
}

// ─── HMM toggle ─────────────────────────────────────────────────────────────

TEST(QuerySegmentNeoTest, NoHmm) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto sentence = std::string_view{"中国科学院"};

    auto result_hmm = QuerySegment<>::cut(dict, model, sentence);
    auto result_no = QuerySegment<false>::cut(dict, model, sentence);

    // For this all-dict sentence, HMM vs no-HMM should produce the same result.
    EXPECT_EQ(join(result_hmm), join(result_no));
}
