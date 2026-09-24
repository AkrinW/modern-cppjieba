#include "../TestUtils.hpp"
#include "gtest/gtest.h"
#include "neo/HMMSegment.hpp"
#include "neo/HMModel.hpp"
#include "neo/Unicode.hpp"

#include "test_paths.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using namespace neo_cppjieba;

inline constexpr auto HMM_MODEL_FILE = std::string_view{DICT_DIR "/hmm_model.utf8"};

namespace {

// Model parsing tests own their input files in an isolated temporary directory.
class HMModelTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto pattern = (std::filesystem::temp_directory_path() / "neo-hmm-model-XXXXXX").string();
        ASSERT_NE(::mkdtemp(pattern.data()), nullptr);
        directory_ = std::move(pattern);
        write_model(model_lines_);
    }

    void TearDown() override {
        if (!directory_.empty()) {
            auto error = std::error_code{};
            std::filesystem::remove_all(directory_, error);
            EXPECT_FALSE(error) << error.message();
        }
    }

    [[nodiscard]] auto model_path() const -> std::string {
        return (directory_ / "model.utf8").string();
    }

    void write_model(std::span<const std::string> lines) const {
        auto output = std::ofstream{model_path(), std::ios::binary};
        ASSERT_TRUE(output.is_open());
        for (const auto &line : lines) {
            output << line << '\n';
        }
        output.close();
        ASSERT_TRUE(output.good());
    }

    const std::vector<std::string> model_lines_{
        "-1 -2 -3 -4", "-1 -2 -3 -4", "-5 -6 -7 -8", "-9 -10 -11 -12", "-13 -14 -15 -16",
        "甲:-1,𠮷:0",  "甲:-2",       "甲:-3",       "甲:-4",
    };
    std::filesystem::path directory_;
};

} // namespace

TEST(HMMStateTest, LabelsRemainAvailableAtCompileTime) {
    constexpr auto labels = std::array{get_hmm_state_label(HMMState::B), get_hmm_state_label(HMMState::E),
                                       get_hmm_state_label(HMMState::M), get_hmm_state_label(HMMState::S)};
    EXPECT_EQ(labels, (std::array{'B', 'E', 'M', 'S'}));
}

TEST_F(HMModelTest, LoadsProbabilitiesForEveryState) {
    const auto model = HMModel{model_path()};
    constexpr auto states = std::array{HMMState::B, HMMState::E, HMMState::M, HMMState::S};
    for (auto i = size_t{0}; i < states.size(); ++i) {
        EXPECT_DOUBLE_EQ(model.get_start_prob(states[i]), -static_cast<double>(i + 1));
        EXPECT_DOUBLE_EQ(model.get_emit_prob(states[i], U'甲'), -static_cast<double>(i + 1));
        for (auto j = size_t{0}; j < states.size(); ++j) {
            EXPECT_DOUBLE_EQ(model.get_trans_prob(states[i], states[j]),
                             -static_cast<double>(i * states.size() + j + 1));
        }
    }
    EXPECT_DOUBLE_EQ(model.get_emit_prob(HMMState::B, U'𠮷'), 0.0);
}

TEST_F(HMModelTest, AcceptsSupportedProbabilityBounds) {
    auto lines = model_lines_;
    lines[0] = "0 -3.14e100 -1 -2";
    lines[1] = "0 -3.14e100 -1 -2";
    lines[5] = "甲:0,𠮷:-3.14e100";
    ASSERT_NO_FATAL_FAILURE(write_model(lines));
    const auto model = HMModel{model_path()};
    EXPECT_DOUBLE_EQ(model.get_start_prob(HMMState::B), 0.0);
    EXPECT_DOUBLE_EQ(model.get_start_prob(HMMState::E), MIN_DOUBLE);
    EXPECT_DOUBLE_EQ(model.get_trans_prob(HMMState::B, HMMState::B), 0.0);
    EXPECT_DOUBLE_EQ(model.get_trans_prob(HMMState::B, HMMState::E), MIN_DOUBLE);
    EXPECT_DOUBLE_EQ(model.get_emit_prob(HMMState::B, U'甲'), 0.0);
    EXPECT_DOUBLE_EQ(model.get_emit_prob(HMMState::B, U'𠮷'), MIN_DOUBLE);
}

TEST_F(HMModelTest, RejectsInvalidLogProbabilitiesInEveryParameterSet) {
    for (const auto token : {"nan", "inf", "-inf", "0.1", "-3.15e100"}) {
        for (auto row = size_t{0}; row < model_lines_.size(); ++row) {
            auto lines = model_lines_;
            lines[row] = row < 5 ? std::string{token} + " -1 -1 -1" : "甲:" + std::string{token};
            ASSERT_NO_FATAL_FAILURE(write_model(lines));
            EXPECT_THROW((HMModel{model_path()}), LogConfig::Exception)
                << "data line " << row + 1 << ", value " << token;
        }
    }
}

TEST_F(HMModelTest, RejectsMalformedProbabilityTokens) {
    for (const auto token : {"", "-1oops", "1e400"}) {
        for (const auto row : {size_t{0}, size_t{1}, size_t{5}}) {
            auto lines = model_lines_;
            lines[row] = row < 5 ? std::string{token} + " -1 -1 -1" : "甲:" + std::string{token};
            ASSERT_NO_FATAL_FAILURE(write_model(lines));
            EXPECT_THROW((HMModel{model_path()}), LogConfig::Exception)
                << "data line " << row + 1 << ", value " << token;
        }
    }
}

TEST_F(HMModelTest, RejectsWrongProbabilityColumnCounts) {
    for (const auto probability_line : {"0 0 0", "0 0 0 0 0"}) {
        for (auto row = size_t{0}; row < 5; ++row) {
            auto lines = model_lines_;
            lines[row] = probability_line;
            ASSERT_NO_FATAL_FAILURE(write_model(lines));
            EXPECT_THROW((HMModel{model_path()}), LogConfig::Exception) << "data line " << row + 1;
        }
    }
}

TEST_F(HMModelTest, RejectsMissingModelData) {
    for (const auto count : {size_t{0}, model_lines_.size() - 1}) {
        ASSERT_NO_FATAL_FAILURE(write_model(std::span<const std::string>{model_lines_}.first(count)));
        EXPECT_THROW((HMModel{model_path()}), LogConfig::Exception);
    }
}

TEST_F(HMModelTest, RejectsAdditionalModelData) {
    auto lines = model_lines_;
    lines.push_back("甲:-1");
    ASSERT_NO_FATAL_FAILURE(write_model(lines));
    EXPECT_THROW((HMModel{model_path()}), LogConfig::Exception);
}

TEST_F(HMModelTest, LoadsCrLfModelWithCommentsAndBlankLines) {
    auto lines = model_lines_;
    lines.insert(lines.begin(), {"# header", "", " \t# indented comment", " \t"});
    lines.push_back("# trailing comment");
    for (auto &line : lines) {
        line += '\r';
    }
    ASSERT_NO_FATAL_FAILURE(write_model(lines));
    const auto model = HMModel{model_path()};
    EXPECT_DOUBLE_EQ(model.get_start_prob(HMMState::B), -1.0);
    EXPECT_DOUBLE_EQ(model.get_emit_prob(HMMState::S, U'甲'), -4.0);
}

TEST_F(HMModelTest, RejectsEmptyEmissionTables) {
    for (auto row = size_t{5}; row < model_lines_.size(); ++row) {
        auto lines = model_lines_;
        lines[row] = ",,,";
        ASSERT_NO_FATAL_FAILURE(write_model(lines));
        EXPECT_THROW((HMModel{model_path()}), LogConfig::Exception) << "data line " << row + 1;
    }
}

TEST_F(HMModelTest, RejectsDuplicateEmissionKeys) {
    for (const auto emission_line : {"甲:-1,甲:-2", "甲:-1,甲:-1"}) {
        for (auto row = size_t{5}; row < model_lines_.size(); ++row) {
            auto lines = model_lines_;
            lines[row] = emission_line;
            ASSERT_NO_FATAL_FAILURE(write_model(lines));
            EXPECT_THROW((HMModel{model_path()}), LogConfig::Exception) << "data line " << row + 1;
        }
    }
}

TEST_F(HMModelTest, RejectsEmissionEntriesWithoutAColon) {
    auto lines = model_lines_;
    lines[5] = "甲-1";
    ASSERT_NO_FATAL_FAILURE(write_model(lines));
    EXPECT_THROW((HMModel{model_path()}), LogConfig::Exception);
}

TEST_F(HMModelTest, RejectsEmissionKeysThatAreNotSingleUnicodeScalars) {
    for (const auto emission_line : {":-1", "甲乙:-1", "\xFF:-1"}) {
        auto lines = model_lines_;
        lines[5] = emission_line;
        ASSERT_NO_FATAL_FAILURE(write_model(lines));
        EXPECT_THROW((HMModel{model_path()}), LogConfig::Exception);
    }
}

TEST_F(HMModelTest, AcceptsEmptyEmissionFieldsAroundValidEntries) {
    auto lines = model_lines_;
    lines[5] = ",甲:-1,,𠮷:0,";
    ASSERT_NO_FATAL_FAILURE(write_model(lines));
    const auto model = HMModel{model_path()};
    EXPECT_DOUBLE_EQ(model.get_emit_prob(HMMState::B, U'甲'), -1.0);
    EXPECT_DOUBLE_EQ(model.get_emit_prob(HMMState::B, U'𠮷'), 0.0);
    EXPECT_EQ(model.get_emit_prob_map(HMMState::B).size(), 2u);
}

TEST_F(HMModelTest, MissingRunesUseTheExistingFallback) {
    const auto model = HMModel{model_path()};
    for (const auto state : {HMMState::B, HMMState::E, HMMState::M, HMMState::S}) {
        EXPECT_DOUBLE_EQ(model.get_emit_prob(state, U'外'), MIN_DOUBLE);
    }
}

TEST_F(HMModelTest, UnknownRunesStillProduceContiguousRanges) {
    const auto model = HMModel{model_path()};
    const auto runes = decode("外𠀀");
    const auto ranges = HMMSegment::cut(model, runes);
    EXPECT_EQ(to_strings(runes, ranges), (std::vector<std::string>{"外", "𠀀"}));
}

TEST(HMMSegmentTest, EmptyInput) {
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = Unicode{};
    auto result = HMMSegment::cut(model, std::span<const Rune>{runes});
    EXPECT_TRUE(result.empty());
}

TEST(HMMSegmentTest, ChineseWithPunctuation) {
    auto model = HMModel{HMM_MODEL_FILE};
    auto sentence = std::string{"我来自北京邮电大学。。。学号123456"};
    auto runes = decode(sentence);
    auto result = HMMSegment::cut(model, runes);
    auto words = to_strings(runes, result);
    auto expected = std::vector<std::string>{"我来", "自北京", "邮电大学", "。", "。", "。", "学号", "123456"};
    EXPECT_EQ(words, expected) << "got: " << join(words);
}

TEST(HMMSegmentTest, ASCIIWithCommas) {
    auto model = HMModel{HMM_MODEL_FILE};
    auto sentence = std::string{"IBM,1.2,123"};
    auto runes = decode(sentence);
    auto result = HMMSegment::cut(model, runes);
    auto words = to_strings(runes, result);
    auto expected = std::vector<std::string>{"IBM", ",", "1.2", ",", "123"};
    EXPECT_EQ(words, expected) << "got: " << join(words);
}

TEST(HMMSegmentTest, PureChinese) {
    auto model = HMModel{HMM_MODEL_FILE};
    auto sentence = std::string{"我来自北京邮电大学"};
    auto runes = decode(sentence);
    auto ranges = HMMSegment::cut(model, runes);
    auto words = to_strings(std::span<const Rune>{runes}, ranges);
    EXPECT_FALSE(words.empty());
    auto reconstructed = std::string{};
    for (auto &&w : words) {
        reconstructed += w;
    }
    EXPECT_EQ(reconstructed, sentence);
}

TEST(HMMSegmentTest, SingleCharacter) {
    auto model = HMModel{HMM_MODEL_FILE};
    auto runes = decode(std::string_view{"我"});
    auto result = HMMSegment::cut(model, runes);
    auto words = to_strings(runes, result);
    ASSERT_EQ(words.size(), 1);
    EXPECT_EQ(words[0], "我");
}

TEST(HMMSegmentTest, PureASCII) {
    auto model = HMModel{HMM_MODEL_FILE};
    auto sentence = std::string{"Hello123World"};
    auto runes = decode(sentence);
    auto result = HMMSegment::cut(model, runes);
    auto words = to_strings(runes, result);
    EXPECT_FALSE(words.empty());
    auto reconstructed = std::string{};
    for (auto &&w : words) {
        reconstructed += w;
    }
    EXPECT_EQ(reconstructed, sentence);
}
