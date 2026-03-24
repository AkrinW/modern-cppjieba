#include "../TestUtils.hpp"
#include "gtest/gtest.h"
#include "neo/Dag.hpp"
#include "neo/DictTrie.hpp"
#include "neo/HMModel.hpp"
#include "neo/MixSegment.hpp"
#include "neo/StringUtil.hpp"
#include "neo/Unicode.hpp"

#include "test_paths.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace neo_cppjieba;

inline constexpr auto DICT_FILE = std::string_view{DICT_DIR "/jieba.dict.utf8"};
inline constexpr auto HMM_MODEL_FILE = std::string_view{DICT_DIR "/hmm_model.utf8"};

TEST(MixSegmentNeoTest, EmptyInput) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = Unicode{};
    auto result = MixSegment<true>::cut(dict, model, runes);
    EXPECT_TRUE(result.empty());
}

TEST(MixSegmentNeoTest, SingleChar) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"我"});
    auto result = MixSegment<>::cut(dict, model, runes);
    auto words = to_strings(runes, result);
    ASSERT_EQ(words.size(), 1);
    EXPECT_EQ(words[0], "我");
}

TEST(MixSegmentNeoTest, SpanSubrangeUsesLocalOffsets) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"甲我来自北京邮电大学乙"});
    auto span = std::span<const Rune>{runes}.subspan(1, runes.size() - 2);
    auto result = MixSegment<>::cut(dict, model, span);
    auto words = to_strings(span, result);

    EXPECT_EQ(words, std::vector<std::string>({"我", "来自", "北京邮电大学"}));
    ASSERT_FALSE(result.empty());
    EXPECT_EQ(result.front().begin, 0u);
    EXPECT_EQ(result.back().end, static_cast<uint32_t>(span.size()));
}

TEST(MixSegmentNeoTest, ClassicSentence) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"我来自北京邮电大学"});
    auto result = MixSegment<>::cut(dict, model, runes);
    auto words = to_strings(runes, result);

    auto expected = std::vector<std::string>{"我", "来自", "北京邮电大学"};
    EXPECT_EQ(words, expected) << "actual: " << join(words);
}

TEST(MixSegmentNeoTest, HangyanBuilding) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"他来到了网易杭研大厦"});
    auto result = MixSegment<>::cut(dict, model, runes);
    auto words = to_strings(runes, result);

    EXPECT_EQ(join(words), "他/来到/了/网易/杭研/大厦") << "actual: " << join(words);
}

TEST(MixSegmentNeoTest, HangyanBuildingNoHMM) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"他来到了网易杭研大厦"});
    auto result = MixSegment<false>::cut(dict, model, runes);
    auto words = to_strings(runes, result);

    EXPECT_EQ(join(words), "他/来到/了/网易/杭/研/大厦") << "actual: " << join(words);
}

TEST(MixSegmentNeoTest, UnicodeOverloadWithSeparators) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto sentence = std::string_view{"我来自北京邮电大学。。。学号123456，用AK47"};
    auto runes = decode(sentence);
    auto result = MixSegment<>::cut(dict, model, runes);
    auto words = to_strings(runes, result);
    auto expected =
        std::vector<std::string>{"我", "来自", "北京邮电大学", "。", "。", "。", "学号", "123456", "，", "用", "AK47"};
    EXPECT_EQ(words, expected) << "actual: " << join(words);
}

TEST(MixSegmentNeoTest, BChaoTShirt) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"B超 T恤"});
    auto result = MixSegment<>::cut(dict, model, runes);
    auto words = to_strings(runes, result);

    EXPECT_EQ(join(words), "B超/ /T恤") << "actual: " << join(words);
}

TEST(MixSegmentNeoTest, Unicode32Emoji) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"天气很好，🙋 我们去郊游。"});
    auto result = MixSegment<>::cut(dict, model, runes);
    auto words = to_strings(runes, result);

    EXPECT_EQ(join(words), "天气/很/好/，/🙋/ /我们/去/郊游/。") << "actual: " << join(words);
}

TEST(MixSegmentNeoTest, WordRangeContiguous) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"小明硕士毕业于中国科学院计算所"});
    auto result = MixSegment<>::cut(dict, model, runes);

    ASSERT_FALSE(result.empty());
    EXPECT_EQ(result.front().begin, 0u);

    for (size_t i = 1; i < result.size(); ++i) {
        EXPECT_EQ(result[i].begin, result[i - 1].end) << "gap between word " << (i - 1) << " and " << i;
    }
    EXPECT_EQ(result.back().end, static_cast<uint32_t>(runes.size()));
}

TEST(MixSegmentNeoTest, ReconstructsOriginal) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto sentence = std::string{"南京市长江大桥，欢迎你来参观游览。"};
    auto runes = decode(sentence);
    auto result = MixSegment<>::cut(dict, model, runes);
    auto words = to_strings(runes, result);

    auto reconstructed = std::string{};
    for (auto &&w : words) {
        reconstructed += w;
    }
    EXPECT_EQ(reconstructed, sentence);
}

TEST(MixSegmentNeoTest, NanjingBridge) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"南京市长江大桥"});
    auto result = MixSegment<>::cut(dict, model, runes);
    auto words = to_strings(runes, result);

    EXPECT_EQ(join(words), "南京市/长江大桥") << "actual: " << join(words);
}

TEST(MixSegmentNeoTest, PureASCII) {
    auto dict = DictTrie{DICT_FILE};
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"IBM,3.14"});
    auto result = MixSegment<>::cut(dict, model, runes);
    auto words = to_strings(runes, result);

    EXPECT_EQ(join(words), "IBM/,/3.14") << "actual: " << join(words);
}
