#include "../TestUtils.hpp"
#include "gtest/gtest.h"
#include "neo/Jieba.hpp"

#include "test_paths.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace neo_cppjieba;

inline constexpr auto DICT_FILE = std::string_view{DICT_DIR "/jieba.dict.utf8"};
inline constexpr auto HMM_MODEL_FILE = std::string_view{DICT_DIR "/hmm_model.utf8"};

TEST(JiebaNeoTest, DirectStringCutReturnsInputEncoding) {
    auto jieba = Jieba{DICT_FILE, HMM_MODEL_FILE};
    auto words = jieba.cut(std::string_view{"他来到了网易杭研大厦"});

    auto expected = std::vector<std::string>{"他", "来到", "了", "网易", "杭研", "大厦"};
    EXPECT_EQ(words, expected);
}

TEST(JiebaNeoTest, DirectUtf16CutReturnsUtf16Words) {
    auto jieba = Jieba{DICT_FILE, HMM_MODEL_FILE};
    auto words = jieba.cut(std::u16string_view{u"我来自北京邮电大学"});

    auto expected = std::vector<std::u16string>{u"我", u"来自", u"北京邮电大学"};
    EXPECT_EQ(words, expected);
}

TEST(JiebaNeoTest, DirectSpanCutReturnsWordRanges) {
    auto jieba = Jieba{DICT_FILE, HMM_MODEL_FILE};
    auto runes = jieba.decode(std::string_view{"甲中国科学院乙"});
    auto span = std::span<const Rune>{runes}.subspan(1, runes.size() - 2);
    auto ranges = jieba.cut<CutMethod::SEARCH>(span);
    auto words = Jieba::encode_words(span, ranges);

    EXPECT_EQ(words, std::vector<std::string>({"中国", "科学", "学院", "科学院", "中国科学院"}));
}

TEST(JiebaNeoTest, ManualUnicodePipelineReturnsWordRanges) {
    auto jieba = Jieba{DICT_FILE, HMM_MODEL_FILE};
    auto runes = jieba.decode(std::string_view{"我来自北京邮电大学"});
    auto ranges = jieba.cut<CutMethod::FULL>(runes);
    auto words = Jieba::encode_words(runes, ranges);

    auto expected =
        std::vector<std::string>{"我", "来自", "北京", "北京邮电", "北京邮电大学", "邮电", "邮电大学", "电大", "大学"};
    EXPECT_EQ(words, expected) << "actual: " << join(words);
}

TEST(JiebaNeoTest, ManualOffsetPipelineMatchesDirectSearchCut) {
    auto jieba = Jieba{DICT_FILE, HMM_MODEL_FILE};
    auto sentence = std::string_view{"中国科学院，后在日本京都大学深造"};
    auto decoded = jieba.decode_with_offset(sentence);
    auto ranges = jieba.cut<CutMethod::SEARCH>(decoded);
    auto fast_words = Jieba::encode_words(as_view(sentence), decoded.offsets, ranges);
    auto direct_words = jieba.cut<CutMethod::SEARCH>(sentence);

    EXPECT_EQ(fast_words, direct_words) << "fast: " << join(fast_words) << "\ndirect: " << join(direct_words);
}

TEST(JiebaNeoTest, GenericCutMethodAndNoHmmTemplate) {
    auto jieba = Jieba{DICT_FILE, HMM_MODEL_FILE};
    auto words = jieba.cut<CutMethod::MIX, false>(std::string_view{"他来到了网易杭研大厦"});
    EXPECT_EQ(join(words), "他/来到/了/网易/杭/研/大厦");
}

TEST(JiebaNeoTest, DefaultAndExplicitCutMethodsWork) {
    auto jieba = Jieba{DICT_FILE, HMM_MODEL_FILE};
    auto sentence = std::string_view{"中国科学院"};

    EXPECT_EQ(jieba.cut(sentence), jieba.cut<CutMethod::MIX>(sentence));
    EXPECT_EQ(jieba.cut<CutMethod::SEARCH>(sentence),
              std::vector<std::string>({"中国", "科学", "学院", "科学院", "中国科学院"}));
    EXPECT_EQ(jieba.cut<CutMethod::MP>(sentence), std::vector<std::string>({"中国科学院"}));
    EXPECT_EQ(jieba.cut<CutMethod::HMM>(sentence), std::vector<std::string>({"中国", "科学院"}));
}
