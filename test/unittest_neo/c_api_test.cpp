#include "neo/Jieba.hpp"

#include "neo_cppjieba.h"
#include "test_paths.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

namespace {

constexpr auto paths = neo_jieba_paths_v1{DICT_DIR "/jieba.dict.utf8", DICT_DIR "/hmm_model.utf8", ""};
using EngineOwner = std::unique_ptr<neo_jieba_engine_v1, decltype(&neo_jieba_v1_engine_destroy)>;
using TokensOwner = std::unique_ptr<neo_jieba_tokens_v1, decltype(&neo_jieba_v1_tokens_destroy)>;

// A shared engine amortizes dictionary loading; every test owns its diagnostics and results.
class CApiTest : public testing::Test {
protected:
    static void SetUpTestSuite() {
        auto storage = std::array<char, 512>{};
        auto diagnostic = neo_jieba_error_buffer_v1{storage.data(), storage.size(), 0};
        neo_jieba_engine_v1 *created = nullptr;
        ASSERT_EQ(neo_jieba_v1_engine_create(&paths, &created, &diagnostic), NEO_JIEBA_V1_OK) << storage.data();
        engine.reset(created);
    }

    static void TearDownTestSuite() {
        engine.reset();
    }

    auto cut(std::string_view input, neo_jieba_mode_v1 mode) -> TokensOwner {
        neo_jieba_tokens_v1 *created = nullptr;
        EXPECT_EQ(neo_jieba_v1_cut_utf8_owned(engine.get(), input.data(), input.size(), mode, &created, &error),
                  NEO_JIEBA_V1_OK)
            << error_storage.data();
        return TokensOwner{created, neo_jieba_v1_tokens_destroy};
    }

    auto borrow(const TokensOwner &tokens) -> neo_jieba_borrowed_tokens_v1 {
        auto data = neo_jieba_borrowed_tokens_v1{};
        EXPECT_EQ(neo_jieba_v1_tokens_borrow_data(tokens.get(), &data, &error), NEO_JIEBA_V1_OK);
        return data;
    }

    inline static auto engine = EngineOwner{nullptr, neo_jieba_v1_engine_destroy};
    std::array<char, 512> error_storage{};
    neo_jieba_error_buffer_v1 error{error_storage.data(), error_storage.size(), 0};
};

// Copy words only in the test harness; the public result itself contains byte ranges.
auto words(const neo_jieba_borrowed_tokens_v1 &data) -> std::vector<std::string> {
    auto result = std::vector<std::string>{};
    result.reserve(data.count);
    for (uint64_t index = 0; index < data.count; ++index) {
        const auto &range = data.ranges[index];
        result.emplace_back(data.utf8_data + range.byte_offset, range.byte_length);
    }
    return result;
}

TEST_F(CApiTest, AllModesPreserveCppSegmentationAndByteRanges) {
    // Each mapping exercises the frozen C value against the independently typed C++ API.
    struct ModeCase {
        neo_jieba_mode_v1 c_mode;
        neo_cppjieba::CutMode cpp_mode;
    };
    const auto modes = std::array{ModeCase{NEO_JIEBA_V1_MIX, neo_cppjieba::CutMode::MIX},
                                  ModeCase{NEO_JIEBA_V1_MIX_NO_HMM, neo_cppjieba::CutMode::MIX_NO_HMM},
                                  ModeCase{NEO_JIEBA_V1_FULL, neo_cppjieba::CutMode::FULL},
                                  ModeCase{NEO_JIEBA_V1_SEARCH, neo_cppjieba::CutMode::SEARCH},
                                  ModeCase{NEO_JIEBA_V1_SEARCH_NO_HMM, neo_cppjieba::CutMode::SEARCH_NO_HMM},
                                  ModeCase{NEO_JIEBA_V1_HMM, neo_cppjieba::CutMode::HMM},
                                  ModeCase{NEO_JIEBA_V1_MP, neo_cppjieba::CutMode::MP}};
    const auto reference = neo_cppjieba::Jieba{paths.dict_path, paths.model_path, paths.user_dict_path};
    const auto input = std::string_view{"他来到了网易杭研大厦，中国科学院😀𠀀"};
    for (const auto &mode : modes) {
        SCOPED_TRACE(mode.c_mode);
        const auto expected = reference.cut(input, mode.cpp_mode);
        const auto owned = cut(input, mode.c_mode);
        ASSERT_NE(owned, nullptr);
        const auto data = borrow(owned);
        ASSERT_EQ(data.count, expected.size());
        EXPECT_EQ(std::string_view(data.utf8_data, data.utf8_size), input);
        for (uint64_t index = 0; index < data.count; ++index) {
            EXPECT_EQ(data.ranges[index].byte_offset, expected[index].position.source.begin);
            EXPECT_EQ(data.ranges[index].byte_length, expected[index].position.source.size());
        }
    }
}

TEST_F(CApiTest, MixRecognizesUnknownWordWithHmm) {
    const auto owned = cut("他来到了网易杭研大厦", NEO_JIEBA_V1_MIX);
    ASSERT_NE(owned, nullptr);
    EXPECT_EQ(words(borrow(owned)), (std::vector<std::string>{"他", "来到", "了", "网易", "杭研", "大厦"}));
}

TEST_F(CApiTest, MixWithoutHmmKeepsUnknownRunesSeparate) {
    const auto owned = cut("他来到了网易杭研大厦", NEO_JIEBA_V1_MIX_NO_HMM);
    ASSERT_NE(owned, nullptr);
    EXPECT_EQ(words(borrow(owned)), (std::vector<std::string>{"他", "来到", "了", "网易", "杭", "研", "大厦"}));
}

TEST_F(CApiTest, UnicodeAndEmbeddedNulRetainExactByteRanges) {
    constexpr char input[] = "😀\0𠀀中国";
    const auto owned = cut(std::string_view{input, sizeof(input) - 1}, NEO_JIEBA_V1_MP);
    ASSERT_NE(owned, nullptr);
    const auto data = borrow(owned);
    EXPECT_EQ(std::string_view(data.utf8_data, data.utf8_size), (std::string_view{input, sizeof(input) - 1}));
    const auto expected = std::array{neo_jieba_token_range_v1{0, 4}, neo_jieba_token_range_v1{4, 1},
                                     neo_jieba_token_range_v1{5, 4}, neo_jieba_token_range_v1{9, 6}};
    ASSERT_EQ(data.count, expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index) {
        EXPECT_EQ(data.ranges[index].byte_offset, expected[index].byte_offset);
        EXPECT_EQ(data.ranges[index].byte_length, expected[index].byte_length);
    }
}

TEST_F(CApiTest, EmptyInputProducesEmptyOwnedResultsInEveryMode) {
    for (const auto mode : {NEO_JIEBA_V1_MIX, NEO_JIEBA_V1_MIX_NO_HMM, NEO_JIEBA_V1_FULL, NEO_JIEBA_V1_SEARCH,
                            NEO_JIEBA_V1_SEARCH_NO_HMM, NEO_JIEBA_V1_HMM, NEO_JIEBA_V1_MP}) {
        const auto owned = cut({}, mode);
        ASSERT_NE(owned, nullptr);
        const auto data = borrow(owned);
        EXPECT_EQ(data.utf8_data, nullptr);
        EXPECT_EQ(data.utf8_size, 0);
        EXPECT_EQ(data.ranges, nullptr);
        EXPECT_EQ(data.count, 0);
    }
}

TEST_F(CApiTest, ResultSurvivesInputAndEngineDestruction) {
    auto owned = TokensOwner{nullptr, neo_jieba_v1_tokens_destroy};
    auto borrowed = neo_jieba_borrowed_tokens_v1{};
    {
        neo_jieba_engine_v1 *created = nullptr;
        ASSERT_EQ(neo_jieba_v1_engine_create(&paths, &created, &error), NEO_JIEBA_V1_OK);
        const auto local_engine = EngineOwner{created, neo_jieba_v1_engine_destroy};
        auto input = std::string{"中国科学院"};
        neo_jieba_tokens_v1 *result = nullptr;
        ASSERT_EQ(neo_jieba_v1_cut_utf8_owned(local_engine.get(), input.data(), input.size(), NEO_JIEBA_V1_MIX, &result,
                                              &error),
                  NEO_JIEBA_V1_OK);
        owned.reset(result);
        borrowed = borrow(owned);
        input.assign(input.size(), 'x');
    }
    EXPECT_EQ(words(borrowed), (std::vector<std::string>{"中国科学院"}));
    EXPECT_EQ(borrow(owned).utf8_data, borrowed.utf8_data);
}

TEST_F(CApiTest, LaterCallsDoNotInvalidateEarlierBorrowedData) {
    const auto first = cut("中国科学院", NEO_JIEBA_V1_MIX);
    ASSERT_NE(first, nullptr);
    const auto borrowed = borrow(first);
    {
        const auto second = cut("他来到了网易杭研大厦", NEO_JIEBA_V1_SEARCH);
        ASSERT_NE(second, nullptr);
    }
    EXPECT_EQ(words(borrowed), (std::vector<std::string>{"中国科学院"}));
}

TEST_F(CApiTest, InvalidUtf8ReturnsAnErrorWithoutPublishingAResult) {
    const auto input = std::string_view{"中国\xE4\xB8"};
    neo_jieba_tokens_v1 *result = nullptr;
    EXPECT_EQ(neo_jieba_v1_cut_utf8_owned(engine.get(), input.data(), input.size(), NEO_JIEBA_V1_MIX, &result, &error),
              NEO_JIEBA_V1_OPERATION_FAILED);
    EXPECT_EQ(result, nullptr);
    EXPECT_GT(error.message_size, 0);
    EXPECT_NE(error_storage[0], '\0');
}

TEST_F(CApiTest, SuccessfulCallClearsPreviousDiagnostics) {
    neo_jieba_tokens_v1 *result = nullptr;
    ASSERT_EQ(neo_jieba_v1_cut_utf8_owned(engine.get(), "x", 1, 256, &result, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    ASSERT_GT(error.message_size, 0);
    const auto owned = cut("中国", NEO_JIEBA_V1_MP);
    ASSERT_NE(owned, nullptr);
    EXPECT_EQ(error.message_size, 0);
    EXPECT_EQ(error_storage[0], '\0');
}

TEST_F(CApiTest, UnknownModeValuesAreRejectedBeforeNarrowing) {
    for (const auto mode : {uint32_t{0}, uint32_t{257}, std::numeric_limits<uint32_t>::max()}) {
        neo_jieba_tokens_v1 *result = nullptr;
        EXPECT_EQ(neo_jieba_v1_cut_utf8_owned(engine.get(), "x", 1, mode, &result, &error),
                  NEO_JIEBA_V1_INVALID_ARGUMENT);
        EXPECT_EQ(result, nullptr);
    }
}

TEST_F(CApiTest, OversizeInputIsRejectedBeforeReadingItsBytes) {
    neo_jieba_tokens_v1 *result = nullptr;
    EXPECT_EQ(neo_jieba_v1_cut_utf8_owned(engine.get(), "x", neo_jieba_v1_max_input_bytes() + 1, NEO_JIEBA_V1_MP,
                                          &result, &error),
              NEO_JIEBA_V1_LIMIT_EXCEEDED);
    EXPECT_EQ(result, nullptr);
}

TEST_F(CApiTest, MissingFilesProduceReadableCreationErrors) {
    constexpr auto missing = TEST_DATA_DIR "/c_api_nonexistent_file";
    for (const auto &invalid :
         {neo_jieba_paths_v1{missing, paths.model_path, ""}, neo_jieba_paths_v1{paths.dict_path, missing, ""},
          neo_jieba_paths_v1{paths.dict_path, paths.model_path, missing}}) {
        neo_jieba_engine_v1 *result = nullptr;
        EXPECT_EQ(neo_jieba_v1_engine_create(&invalid, &result, &error), NEO_JIEBA_V1_OPERATION_FAILED);
        EXPECT_EQ(result, nullptr);
        EXPECT_NE(std::string_view{error_storage.data()}.find(missing), std::string_view::npos);
    }
}

TEST_F(CApiTest, InvalidArgumentsReturnStatusAndClearBorrowedOutput) {
    neo_jieba_tokens_v1 *result = nullptr;
    EXPECT_EQ(neo_jieba_v1_cut_utf8_owned(engine.get(), nullptr, 1, NEO_JIEBA_V1_MP, &result, &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_cut_utf8_owned(nullptr, "x", 1, NEO_JIEBA_V1_MP, &result, &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_cut_utf8_owned(engine.get(), "x", 1, NEO_JIEBA_V1_MP, nullptr, &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    auto borrowed = neo_jieba_borrowed_tokens_v1{"x", 1, nullptr, 99};
    EXPECT_EQ(neo_jieba_v1_tokens_borrow_data(nullptr, &borrowed, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(borrowed.utf8_data, nullptr);
    EXPECT_EQ(borrowed.utf8_size, 0);
    EXPECT_EQ(borrowed.ranges, nullptr);
    EXPECT_EQ(borrowed.count, 0);
}

TEST_F(CApiTest, MissingPathArgumentsAreRejected) {
    neo_jieba_engine_v1 *result = nullptr;
    const auto invalid = neo_jieba_paths_v1{paths.dict_path, paths.model_path, nullptr};
    EXPECT_EQ(neo_jieba_v1_engine_create(&invalid, &result, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_engine_create(nullptr, &result, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_engine_create(&paths, nullptr, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(result, nullptr);
}

TEST_F(CApiTest, TruncatedDiagnosticsRemainTerminatedAndReportFullSize) {
    auto storage = std::array<char, 5>{'x', 'x', 'x', 'x', '!'};
    auto tiny = neo_jieba_error_buffer_v1{storage.data(), 4, 0};
    neo_jieba_tokens_v1 *result = nullptr;
    EXPECT_EQ(neo_jieba_v1_cut_utf8_owned(engine.get(), "x", 1, 0, &result, &tiny), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(std::string_view{storage.data()}, "Unk");
    EXPECT_GT(tiny.message_size, 3);
    EXPECT_EQ(storage[4], '!');
}

TEST_F(CApiTest, DiagnosticsCanBeLengthOnlyOrOmitted) {
    auto length_only = neo_jieba_error_buffer_v1{nullptr, 0, 0};
    neo_jieba_tokens_v1 *result = nullptr;
    EXPECT_EQ(neo_jieba_v1_cut_utf8_owned(engine.get(), "x", 1, 0, &result, &length_only),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_GT(length_only.message_size, 0);
    EXPECT_EQ(neo_jieba_v1_cut_utf8_owned(engine.get(), "x", 1, 0, &result, nullptr), NEO_JIEBA_V1_INVALID_ARGUMENT);
}

TEST_F(CApiTest, InvalidDiagnosticBufferDoesNotPublishAnOwner) {
    auto invalid = neo_jieba_error_buffer_v1{nullptr, 1, 99};
    neo_jieba_tokens_v1 *result = nullptr;
    EXPECT_EQ(neo_jieba_v1_cut_utf8_owned(engine.get(), "x", 1, NEO_JIEBA_V1_MP, &result, &invalid),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(invalid.message_size, 0);
}

TEST(CApiLifetimeTest, DestroyAcceptsNullHandles) {
    neo_jieba_v1_engine_destroy(nullptr);
    neo_jieba_v1_tokens_destroy(nullptr);
}

} // namespace
