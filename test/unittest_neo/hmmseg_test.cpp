#include "../TestUtils.hpp"
#include "gtest/gtest.h"
#include "neo/HMMSegment.hpp"
#include "neo/HMModel.hpp"
#include "neo/Unicode.hpp"

#include "test_paths.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace neo_cppjieba;

inline constexpr auto HMM_MODEL_FILE = std::string_view{DICT_DIR "/hmm_model.utf8"};

// ─── Basic segmentation ─────────────────────────────────────────────────────

TEST(HMMSegmentTest, EmptyInput) {
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = Unicode{};
    auto result = HMMSegment::cut(model, std::span<const Rune>{runes});
    EXPECT_TRUE(result.empty());
}

TEST(HMMSegmentTest, ChineseWithPunctuation) {
    auto model = HMModel{HMM_MODEL_FILE};
    auto sentence = std::string{"我来自北京邮电大学。。。学号123456"};
    auto result = HMMSegment::cut(model, sentence);
    auto expected = std::vector<std::string>{"我来", "自北京", "邮电大学", "。", "。", "。", "学号", "123456"};
    EXPECT_EQ(result, expected) << "got: " << join(result);
}

TEST(HMMSegmentTest, ASCIIWithCommas) {
    auto model = HMModel{HMM_MODEL_FILE};
    auto sentence = std::string{"IBM,1.2,123"};
    auto result = HMMSegment::cut(model, sentence);
    auto expected = std::vector<std::string>{"IBM", ",", "1.2", ",", "123"};
    EXPECT_EQ(result, expected) << "got: " << join(result);
}

TEST(HMMSegmentTest, PureChinese) {
    auto model = HMModel{HMM_MODEL_FILE};
    auto sentence = std::string{"我来自北京邮电大学"};
    auto runes = decode(sentence);
    auto ranges = HMMSegment::cut(model, runes);
    auto words = to_strings(std::span<const Rune>{runes}, ranges);
    EXPECT_FALSE(words.empty());
    // Verify that concatenation of all words reconstructs the original
    auto reconstructed = std::string{};
    for (auto &&w : words) {
        reconstructed += w;
    }
    EXPECT_EQ(reconstructed, sentence);
}

TEST(HMMSegmentTest, SingleCharacter) {
    auto model = HMModel{HMM_MODEL_FILE};
    auto sentence = std::string{"我"};
    auto result = HMMSegment::cut(model, sentence);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], "我");
}

TEST(HMMSegmentTest, PureASCII) {
    auto model = HMModel{HMM_MODEL_FILE};
    auto sentence = std::string{"Hello123World"};
    auto result = HMMSegment::cut(model, sentence);
    // Should group letters together, then digits with letters
    EXPECT_FALSE(result.empty());
    auto reconstructed = std::string{};
    for (auto &&w : result) {
        reconstructed += w;
    }
    EXPECT_EQ(reconstructed, sentence);
}
