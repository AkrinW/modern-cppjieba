#include "neo/Jieba.hpp"
#include "neo/KeywordExtractor.hpp"

#include "TestUtils.hpp"
#include "neo_cppjieba.h"
#include "test_paths.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

namespace {

constexpr auto paths = neo_jieba_paths_v1{DICT_DIR "/jieba.dict.utf8", DICT_DIR "/hmm_model.utf8", ""};
using EngineOwner = std::unique_ptr<neo_jieba_engine_v1, decltype(&neo_jieba_v1_engine_destroy)>;
using TokensOwner = std::unique_ptr<neo_jieba_tokens_v1, decltype(&neo_jieba_v1_tokens_destroy)>;
using WorkspaceOwner = std::unique_ptr<neo_jieba_workspace_v1, decltype(&neo_jieba_v1_workspace_destroy)>;
using RangesOwner = std::unique_ptr<neo_jieba_ranges_v1, decltype(&neo_jieba_v1_ranges_destroy)>;
using TfIdfOwner = std::unique_ptr<neo_jieba_tfidf_v1, decltype(&neo_jieba_v1_tfidf_destroy)>;
using TextRankOwner = std::unique_ptr<neo_jieba_textrank_v1, decltype(&neo_jieba_v1_textrank_destroy)>;
using KeywordsOwner = std::unique_ptr<neo_jieba_keywords_v1, decltype(&neo_jieba_v1_keywords_destroy)>;

constexpr auto idf_path = std::string_view{DICT_DIR "/idf.utf8"};
constexpr auto stop_words_path = std::string_view{DICT_DIR "/stop_words.utf8"};

// Describe a path as a C source; the path text must outlive the call that reads it.
auto file_source(std::string_view path) -> neo_jieba_source_v1 {
    return {NEO_JIEBA_V1_SOURCE_FILE, 0, path.data(), path.size()};
}

// Describe borrowed contents as a C source.
auto memory_source(std::string_view text) -> neo_jieba_source_v1 {
    return {NEO_JIEBA_V1_SOURCE_MEMORY, 0, text.data(), text.size()};
}

// Load a repository file as the contents a memory source borrows.
auto read_text(std::string_view path) -> std::string {
    auto input = std::ifstream{std::string{path}, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

// A shared engine amortizes dictionary loading; every test owns its diagnostics and results.
class CApiTest : public testing::Test {
protected:
    static void SetUpTestSuite() {
        auto storage = std::array<char, 512>{};
        auto diagnostic = neo_jieba_error_buffer_v1{storage.data(), storage.size(), 0};
        neo_jieba_engine_v1 *created = nullptr;
        ASSERT_EQ(neo_jieba_v1_engine_create(&paths, &created, &diagnostic), NEO_JIEBA_V1_OK) << storage.data();
        engine.reset(created);

        const auto idf = file_source(idf_path);
        const auto stop_words = file_source(stop_words_path);
        neo_jieba_tfidf_v1 *created_tfidf = nullptr;
        ASSERT_EQ(neo_jieba_v1_tfidf_create(&idf, &stop_words, &created_tfidf, &diagnostic), NEO_JIEBA_V1_OK)
            << storage.data();
        tfidf.reset(created_tfidf);
        neo_jieba_textrank_v1 *created_textrank = nullptr;
        ASSERT_EQ(neo_jieba_v1_textrank_create(&stop_words, &created_textrank, &diagnostic), NEO_JIEBA_V1_OK)
            << storage.data();
        textrank.reset(created_textrank);
    }

    static void TearDownTestSuite() {
        textrank.reset();
        tfidf.reset();
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

    auto make_workspace() -> WorkspaceOwner {
        neo_jieba_workspace_v1 *created = nullptr;
        EXPECT_EQ(neo_jieba_v1_workspace_create(&created, &error), NEO_JIEBA_V1_OK) << error_storage.data();
        return WorkspaceOwner{created, neo_jieba_v1_workspace_destroy};
    }

    auto make_ranges() -> RangesOwner {
        neo_jieba_ranges_v1 *created = nullptr;
        EXPECT_EQ(neo_jieba_v1_ranges_create(&created, &error), NEO_JIEBA_V1_OK) << error_storage.data();
        return RangesOwner{created, neo_jieba_v1_ranges_destroy};
    }

    auto borrow_ranges(const RangesOwner &ranges) -> neo_jieba_borrowed_ranges_v1 {
        auto data = neo_jieba_borrowed_ranges_v1{};
        EXPECT_EQ(neo_jieba_v1_ranges_borrow_data(ranges.get(), &data, &error), NEO_JIEBA_V1_OK);
        return data;
    }

    auto create_from_sources(const neo_jieba_source_v1 &dict, const neo_jieba_source_v1 &model,
                             std::span<const neo_jieba_source_v1> user_dicts) -> EngineOwner {
        neo_jieba_engine_v1 *created = nullptr;
        EXPECT_EQ(neo_jieba_v1_engine_create_from_sources(&dict, &model, user_dicts.data(), user_dicts.size(), &created,
                                                          &error),
                  NEO_JIEBA_V1_OK)
            << error_storage.data();
        return EngineOwner{created, neo_jieba_v1_engine_destroy};
    }

    auto create_from_snapshot(const neo_jieba_source_v1 &snapshot, const neo_jieba_source_v1 &model) -> EngineOwner {
        neo_jieba_engine_v1 *created = nullptr;
        EXPECT_EQ(neo_jieba_v1_engine_create_from_snapshot(&snapshot, &model, &created, &error), NEO_JIEBA_V1_OK)
            << error_storage.data();
        return EngineOwner{created, neo_jieba_v1_engine_destroy};
    }

    [[nodiscard]] auto error_message() const -> std::string_view {
        return error_storage.data();
    }

    // Edits run on a clone so the shared engine stays as loaded for every other test.
    auto make_clone() -> EngineOwner {
        neo_jieba_engine_v1 *created = nullptr;
        EXPECT_EQ(neo_jieba_v1_engine_clone(engine.get(), &created, &error), NEO_JIEBA_V1_OK) << error_message();
        return EngineOwner{created, neo_jieba_v1_engine_destroy};
    }

    auto make_keywords() -> KeywordsOwner {
        neo_jieba_keywords_v1 *created = nullptr;
        EXPECT_EQ(neo_jieba_v1_keywords_create(&created, &error), NEO_JIEBA_V1_OK) << error_message();
        return KeywordsOwner{created, neo_jieba_v1_keywords_destroy};
    }

    auto borrow_keywords(const KeywordsOwner &keywords) -> neo_jieba_borrowed_keywords_v1 {
        auto data = neo_jieba_borrowed_keywords_v1{};
        EXPECT_EQ(neo_jieba_v1_keywords_borrow_data(keywords.get(), &data, &error), NEO_JIEBA_V1_OK);
        return data;
    }

    inline static auto engine = EngineOwner{nullptr, neo_jieba_v1_engine_destroy};
    inline static auto tfidf = TfIdfOwner{nullptr, neo_jieba_v1_tfidf_destroy};
    inline static auto textrank = TextRankOwner{nullptr, neo_jieba_v1_textrank_destroy};
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

// Slice the caller's input by borrowed ranges, as a zero-copy caller does.
auto words(std::string_view input, const neo_jieba_borrowed_ranges_v1 &data) -> std::vector<std::string> {
    auto result = std::vector<std::string>{};
    result.reserve(data.count);
    for (uint64_t index = 0; index < data.count; ++index) {
        result.emplace_back(input.substr(data.ranges[index].byte_offset, data.ranges[index].byte_length));
    }
    return result;
}

// A tag's text stops at its NUL padding; a four-byte tag has none.
auto tag_text(const neo_jieba_pos_tag_v1 &tag) -> std::string_view {
    return {std::ranges::begin(tag.data), std::ranges::find(tag.data, '\0')};
}

// Build a NUL-padded tag from at most four bytes of text.
auto c_tag(std::string_view text) -> neo_jieba_pos_tag_v1 {
    auto tag = neo_jieba_pos_tag_v1{};
    std::ranges::copy(text.substr(0, sizeof(tag.data)), std::ranges::begin(tag.data));
    return tag;
}

// Join each word with its tag as "word/tag"; ranges without tags yield no words.
auto tagged_words(std::string_view input, const neo_jieba_borrowed_ranges_v1 &data) -> std::vector<std::string> {
    auto result = std::vector<std::string>{};
    if (data.tags == nullptr) {
        return result;
    }
    result.reserve(data.count);
    for (uint64_t index = 0; index < data.count; ++index) {
        const auto &range = data.ranges[index];
        result.push_back(
            std::format("{}/{}", input.substr(range.byte_offset, range.byte_length), tag_text(data.tags[index])));
    }
    return result;
}

// Both C fill entry points share one signature, so the helpers below run either of them.
using FillRanges = decltype(&neo_jieba_v1_cut_utf8_into);
using ReadRanges = std::vector<std::string> (*)(std::string_view, const neo_jieba_borrowed_ranges_v1 &);

// Enough workers and repetitions for ThreadSanitizer to observe overlapping calls.
constexpr auto worker_count = std::size_t{4};
constexpr auto rounds = std::size_t{8};

constexpr auto all_modes = std::array<neo_jieba_mode_v1, 7>{
    NEO_JIEBA_V1_MIX,           NEO_JIEBA_V1_MIX_NO_HMM, NEO_JIEBA_V1_FULL, NEO_JIEBA_V1_SEARCH,
    NEO_JIEBA_V1_SEARCH_NO_HMM, NEO_JIEBA_V1_HMM,        NEO_JIEBA_V1_MP};

constexpr auto sentences = std::array<std::string_view, 3>{
    "他来到了网易杭研大厦", "小明硕士毕业于中国科学院计算所，后在日本京都大学深造", "去医院做B超，编号123😀"};

// Each call owns its result and diagnostics, as concurrent callers must; a failed call yields no words.
auto cut_words(const neo_jieba_engine_v1 *engine, std::string_view input, neo_jieba_mode_v1 mode)
    -> std::vector<std::string> {
    auto storage = std::array<char, 256>{};
    auto diagnostic = neo_jieba_error_buffer_v1{storage.data(), storage.size(), 0};
    neo_jieba_tokens_v1 *created = nullptr;
    if (neo_jieba_v1_cut_utf8_owned(engine, input.data(), input.size(), mode, &created, &diagnostic)
        != NEO_JIEBA_V1_OK) {
        return {};
    }
    const auto owned = TokensOwner{created, neo_jieba_v1_tokens_destroy};
    auto data = neo_jieba_borrowed_tokens_v1{};
    if (neo_jieba_v1_tokens_borrow_data(owned.get(), &data, &diagnostic) != NEO_JIEBA_V1_OK) {
        return {};
    }
    return words(data);
}

// Cut every sentence in every mode, as one worker sharing the engine does.
auto cut_all(const neo_jieba_engine_v1 *engine) -> std::vector<std::vector<std::string>> {
    auto results = std::vector<std::vector<std::string>>{};
    results.reserve(all_modes.size() * sentences.size());
    for (const auto mode : all_modes) {
        for (const auto sentence : sentences) {
            results.push_back(cut_words(engine, sentence, mode));
        }
    }
    return results;
}

// A failed fill leaves the ranges empty, so it yields no words.
auto fill_words(const neo_jieba_engine_v1 *engine, std::string_view input, neo_jieba_mode_v1 mode,
                neo_jieba_workspace_v1 *workspace, neo_jieba_ranges_v1 *ranges, FillRanges fill, ReadRanges read)
    -> std::vector<std::string> {
    fill(engine, input.data(), input.size(), mode, workspace, ranges, nullptr);
    auto data = neo_jieba_borrowed_ranges_v1{};
    neo_jieba_v1_ranges_borrow_data(ranges, &data, nullptr);
    return read(input, data);
}

// One worker reuses its own workspace and ranges for every call; only the engine is shared.
auto fill_all(const neo_jieba_engine_v1 *engine, FillRanges fill, ReadRanges read)
    -> std::vector<std::vector<std::string>> {
    neo_jieba_workspace_v1 *created_workspace = nullptr;
    neo_jieba_ranges_v1 *created_ranges = nullptr;
    neo_jieba_v1_workspace_create(&created_workspace, nullptr);
    neo_jieba_v1_ranges_create(&created_ranges, nullptr);
    const auto workspace = WorkspaceOwner{created_workspace, neo_jieba_v1_workspace_destroy};
    const auto ranges = RangesOwner{created_ranges, neo_jieba_v1_ranges_destroy};
    auto results = std::vector<std::vector<std::string>>{};
    results.reserve(all_modes.size() * sentences.size());
    for (const auto mode : all_modes) {
        for (const auto sentence : sentences) {
            results.push_back(fill_words(engine, sentence, mode, workspace.get(), ranges.get(), fill, read));
        }
    }
    return results;
}

// CppJieba's TextRank defaults, an unfiltered TF-IDF request, and CppJieba's keyword demo sentence.
constexpr auto default_textrank_options = neo_jieba_textrank_options_v1{5, 10, 0.85, nullptr, 0};
constexpr auto unfiltered_tfidf_options = neo_jieba_tfidf_options_v1{nullptr, 0};
constexpr auto keyword_text =
    std::string_view{"我是拖拉机学院手扶拖拉机专业的。不用多久，我就会升职加薪，当上CEO，走上人生巅峰。"};

// One keyword as plain data, comparable between C and C++ results.
struct KeywordSummary {
    std::string word;
    double weight;
    std::string tag;
    std::vector<std::array<uint64_t, 2>> occurrences;

    auto operator==(const KeywordSummary &) const -> bool = default;
};

// Read keywords as a zero-copy caller does: each word is spelled by its first occurrence in the input.
auto summarize(std::string_view input, const neo_jieba_borrowed_keywords_v1 &data) -> std::vector<KeywordSummary> {
    auto result = std::vector<KeywordSummary>{};
    result.reserve(data.count);
    for (const auto &keyword : std::span{data.keywords, static_cast<std::size_t>(data.count)}) {
        auto &summary = result.emplace_back(KeywordSummary{{}, keyword.weight, std::string{tag_text(keyword.tag)}, {}});
        const auto occurrences =
            std::span{data.occurrences + keyword.first_occurrence, static_cast<std::size_t>(keyword.occurrence_count)};
        for (const auto &range : occurrences) {
            summary.occurrences.push_back({range.byte_offset, range.byte_length});
        }
        if (!occurrences.empty()) {
            summary.word = input.substr(occurrences.front().byte_offset, occurrences.front().byte_length);
        }
    }
    return result;
}

// The same summary for a C++ result, the reference for C extraction.
auto summarize(const std::vector<neo_cppjieba::Keyword<char>> &keywords) -> std::vector<KeywordSummary> {
    auto result = std::vector<KeywordSummary>{};
    result.reserve(keywords.size());
    for (const auto &keyword : keywords) {
        auto &summary = result.emplace_back(
            KeywordSummary{keyword.word, keyword.weight, std::string{keyword.tag.to_string_view()}, {}});
        for (const auto &position : keyword.positions) {
            summary.occurrences.push_back(
                {static_cast<uint64_t>(position.source.begin), static_cast<uint64_t>(position.source.size())});
        }
    }
    return result;
}

// The C++ facade C keyword results are checked against, loaded once.
auto reference_jieba() -> const neo_cppjieba::Jieba & {
    static const auto jieba = neo_cppjieba::Jieba{paths.dict_path, paths.model_path, paths.user_dict_path};
    return jieba;
}

// The C++ TF-IDF extractor loaded from the same files as the fixture's C extractor.
auto reference_tfidf() -> const neo_cppjieba::TfIdfExtractor & {
    static const auto extractor = neo_cppjieba::TfIdfExtractor{idf_path, stop_words_path};
    return extractor;
}

// The C++ TextRank extractor loaded from the same stop words as the fixture's C extractor.
auto reference_textrank() -> const neo_cppjieba::TextRankExtractor & {
    static const auto extractor = neo_cppjieba::TextRankExtractor{stop_words_path};
    return extractor;
}

// One worker extracts every sentence with both rankers through its own handles; engine and extractors are shared.
auto extract_all(const neo_jieba_engine_v1 *engine, const neo_jieba_tfidf_v1 *tfidf,
                 const neo_jieba_textrank_v1 *textrank) -> std::vector<std::vector<KeywordSummary>> {
    neo_jieba_workspace_v1 *created_workspace = nullptr;
    neo_jieba_keywords_v1 *created_keywords = nullptr;
    neo_jieba_v1_workspace_create(&created_workspace, nullptr);
    neo_jieba_v1_keywords_create(&created_keywords, nullptr);
    const auto workspace = WorkspaceOwner{created_workspace, neo_jieba_v1_workspace_destroy};
    const auto keywords = KeywordsOwner{created_keywords, neo_jieba_v1_keywords_destroy};
    auto results = std::vector<std::vector<KeywordSummary>>{};
    results.reserve(2 * sentences.size());
    for (const auto sentence : sentences) {
        auto data = neo_jieba_borrowed_keywords_v1{};
        neo_jieba_v1_tfidf_extract_into(tfidf, engine, sentence.data(), sentence.size(), 5, &unfiltered_tfidf_options,
                                        workspace.get(), keywords.get(), nullptr);
        neo_jieba_v1_keywords_borrow_data(keywords.get(), &data, nullptr);
        results.push_back(summarize(sentence, data));
        neo_jieba_v1_textrank_extract_into(textrank, engine, sentence.data(), sentence.size(), 5,
                                           &default_textrank_options, workspace.get(), keywords.get(), nullptr);
        neo_jieba_v1_keywords_borrow_data(keywords.get(), &data, nullptr);
        results.push_back(summarize(sentence, data));
    }
    return results;
}

// Run work repeatedly on worker_count threads at once; each thread keeps its last result.
template <typename Work>
auto run_workers(const Work &work) -> std::vector<std::invoke_result_t<const Work &>> {
    auto results = std::vector<std::invoke_result_t<const Work &>>(worker_count);
    {
        // Destroying the jthreads joins every worker before the results are read.
        auto workers = std::vector<std::jthread>{};
        workers.reserve(worker_count);
        for (auto &result : results) {
            workers.emplace_back([&work, &result] {
                for (auto round = std::size_t{0}; round < rounds; ++round) {
                    result = work();
                }
            });
        }
    }
    return results;
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

TEST_F(CApiTest, ConcurrentCutsSharingAnEngineMatchSerialCuts) {
    const auto *shared = engine.get();
    const auto expected = cut_all(shared);
    ASSERT_TRUE(std::ranges::none_of(expected, [](const auto &tokens) { return tokens.empty(); }));

    for (const auto &result : run_workers([shared] { return cut_all(shared); })) {
        EXPECT_EQ(result, expected);
    }
}

TEST_F(CApiTest, ConcurrentBorrowsOfOneResultSeeTheSameStorage) {
    const auto owned = cut("小明硕士毕业于中国科学院计算所", NEO_JIEBA_V1_SEARCH);
    ASSERT_NE(owned, nullptr);
    const auto expected = borrow(owned);
    const auto *shared = owned.get();

    // A failed borrow zeroes its descriptor, so the comparisons below also catch failures.
    const auto borrow_shared = [shared] {
        auto data = neo_jieba_borrowed_tokens_v1{};
        neo_jieba_v1_tokens_borrow_data(shared, &data, nullptr);
        return data;
    };
    for (const auto &data : run_workers(borrow_shared)) {
        EXPECT_EQ(data.utf8_data, expected.utf8_data);
        EXPECT_EQ(data.utf8_size, expected.utf8_size);
        EXPECT_EQ(data.ranges, expected.ranges);
        EXPECT_EQ(data.count, expected.count);
    }
}

TEST_F(CApiTest, ConcurrentCutIntoWithSeparateHandlesMatchesSerialCuts) {
    const auto *shared = engine.get();
    const auto expected = cut_all(shared);

    for (const auto &result : run_workers([shared] { return fill_all(shared, neo_jieba_v1_cut_utf8_into, words); })) {
        EXPECT_EQ(result, expected);
    }
}

TEST_F(CApiTest, CutIntoMatchesOwnedByteRangesInEveryMode) {
    const auto workspace = make_workspace();
    const auto ranges = make_ranges();
    ASSERT_NE(workspace, nullptr);
    ASSERT_NE(ranges, nullptr);
    constexpr char text[] = "他来到了网易杭研大厦\0中国科学院😀𠀀";
    const auto input = std::string_view{text, sizeof(text) - 1};
    for (const auto mode : all_modes) {
        SCOPED_TRACE(mode);
        const auto owned = cut(input, mode);
        ASSERT_NE(owned, nullptr);
        const auto expected = borrow(owned);
        ASSERT_EQ(neo_jieba_v1_cut_utf8_into(engine.get(), input.data(), input.size(), mode, workspace.get(),
                                             ranges.get(), &error),
                  NEO_JIEBA_V1_OK)
            << error_storage.data();
        const auto data = borrow_ranges(ranges);
        ASSERT_EQ(data.count, expected.count);
        for (uint64_t index = 0; index < data.count; ++index) {
            EXPECT_EQ(data.ranges[index].byte_offset, expected.ranges[index].byte_offset);
            EXPECT_EQ(data.ranges[index].byte_length, expected.ranges[index].byte_length);
        }
    }
}

TEST_F(CApiTest, CutIntoReplacesThePreviousFill) {
    const auto workspace = make_workspace();
    const auto ranges = make_ranges();
    const auto first = std::string_view{"小明硕士毕业于中国科学院计算所"};
    const auto second = std::string_view{"中国科学院"};
    ASSERT_EQ(neo_jieba_v1_cut_utf8_into(engine.get(), first.data(), first.size(), NEO_JIEBA_V1_SEARCH, workspace.get(),
                                         ranges.get(), &error),
              NEO_JIEBA_V1_OK);
    ASSERT_EQ(neo_jieba_v1_cut_utf8_into(engine.get(), second.data(), second.size(), NEO_JIEBA_V1_MIX, workspace.get(),
                                         ranges.get(), &error),
              NEO_JIEBA_V1_OK);
    EXPECT_EQ(words(second, borrow_ranges(ranges)), (std::vector<std::string>{"中国科学院"}));
}

TEST_F(CApiTest, EmptyInputIntoProducesEmptyRangesInEveryMode) {
    const auto workspace = make_workspace();
    const auto ranges = make_ranges();
    for (const auto mode : all_modes) {
        SCOPED_TRACE(mode);
        ASSERT_EQ(neo_jieba_v1_cut_utf8_into(engine.get(), nullptr, 0, mode, workspace.get(), ranges.get(), &error),
                  NEO_JIEBA_V1_OK);
        const auto data = borrow_ranges(ranges);
        EXPECT_EQ(data.ranges, nullptr);
        EXPECT_EQ(data.count, 0);
    }
}

TEST_F(CApiTest, RangesSurviveWorkspaceAndEngineDestruction) {
    const auto ranges = make_ranges();
    const auto input = std::string_view{"中国科学院"};
    {
        neo_jieba_engine_v1 *created = nullptr;
        ASSERT_EQ(neo_jieba_v1_engine_create(&paths, &created, &error), NEO_JIEBA_V1_OK);
        const auto local_engine = EngineOwner{created, neo_jieba_v1_engine_destroy};
        const auto workspace = make_workspace();
        ASSERT_EQ(neo_jieba_v1_cut_utf8_into(local_engine.get(), input.data(), input.size(), NEO_JIEBA_V1_MIX,
                                             workspace.get(), ranges.get(), &error),
                  NEO_JIEBA_V1_OK);
    }
    EXPECT_EQ(words(input, borrow_ranges(ranges)), (std::vector<std::string>{"中国科学院"}));
}

TEST_F(CApiTest, FailedCutIntoLeavesRangesEmpty) {
    const auto workspace = make_workspace();
    const auto ranges = make_ranges();
    const auto valid = std::string_view{"中国科学院"};
    ASSERT_EQ(neo_jieba_v1_cut_utf8_into(engine.get(), valid.data(), valid.size(), NEO_JIEBA_V1_MIX, workspace.get(),
                                         ranges.get(), &error),
              NEO_JIEBA_V1_OK);
    const auto invalid = std::string_view{"中国\xE4\xB8"};
    EXPECT_EQ(neo_jieba_v1_cut_utf8_into(engine.get(), invalid.data(), invalid.size(), NEO_JIEBA_V1_MIX,
                                         workspace.get(), ranges.get(), &error),
              NEO_JIEBA_V1_OPERATION_FAILED);
    EXPECT_GT(error.message_size, 0);
    EXPECT_EQ(borrow_ranges(ranges).count, 0);
}

TEST_F(CApiTest, CutIntoRejectsMissingHandlesAndUnknownModes) {
    const auto workspace = make_workspace();
    const auto ranges = make_ranges();
    ASSERT_EQ(neo_jieba_v1_cut_utf8_into(engine.get(), "x", 1, NEO_JIEBA_V1_MIX, workspace.get(), ranges.get(), &error),
              NEO_JIEBA_V1_OK);
    EXPECT_EQ(neo_jieba_v1_cut_utf8_into(engine.get(), "x", 1, 0, workspace.get(), ranges.get(), &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(borrow_ranges(ranges).count, 0);
    EXPECT_EQ(neo_jieba_v1_cut_utf8_into(nullptr, "x", 1, NEO_JIEBA_V1_MIX, workspace.get(), ranges.get(), &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(
        neo_jieba_v1_cut_utf8_into(engine.get(), nullptr, 1, NEO_JIEBA_V1_MIX, workspace.get(), ranges.get(), &error),
        NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_cut_utf8_into(engine.get(), "x", 1, NEO_JIEBA_V1_MIX, nullptr, ranges.get(), &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_cut_utf8_into(engine.get(), "x", 1, NEO_JIEBA_V1_MIX, workspace.get(), nullptr, &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
}

TEST_F(CApiTest, OversizeInputIntoIsRejectedBeforeReadingItsBytes) {
    const auto workspace = make_workspace();
    const auto ranges = make_ranges();
    EXPECT_EQ(neo_jieba_v1_cut_utf8_into(engine.get(), "x", neo_jieba_v1_max_input_bytes() + 1, NEO_JIEBA_V1_MP,
                                         workspace.get(), ranges.get(), &error),
              NEO_JIEBA_V1_LIMIT_EXCEEDED);
    EXPECT_EQ(borrow_ranges(ranges).count, 0);
}

TEST_F(CApiTest, RangesBorrowRejectsMissingArgumentsAndZeroesOutput) {
    const auto ranges = make_ranges();
    const auto stale = neo_jieba_token_range_v1{1, 2};
    auto data = neo_jieba_borrowed_ranges_v1{&stale, 1};
    EXPECT_EQ(neo_jieba_v1_ranges_borrow_data(nullptr, &data, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(data.ranges, nullptr);
    EXPECT_EQ(data.count, 0);
    EXPECT_EQ(neo_jieba_v1_ranges_borrow_data(ranges.get(), nullptr, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
}

TEST_F(CApiTest, HandleCreationRequiresOutputStorage) {
    EXPECT_EQ(neo_jieba_v1_workspace_create(nullptr, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_ranges_create(nullptr, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
}

TEST_F(CApiTest, MemorySourcesLoadLikeFilesAndAreNotRetained) {
    auto dict_text = read_text(paths.dict_path);
    auto model_text = read_text(paths.model_path);
    const auto from_memory = create_from_sources(memory_source(dict_text), memory_source(model_text), {});
    ASSERT_NE(from_memory, nullptr);
    dict_text.assign(dict_text.size(), 'x');
    model_text.assign(model_text.size(), 'x');
    EXPECT_EQ(cut_all(from_memory.get()), cut_all(engine.get()));
}

TEST_F(CApiTest, FileAndMemoryUserDictionariesMatchCppLoading) {
    const auto user_text = std::string{"杭研大厦\n"};
    const auto users = std::array{memory_source(user_text), file_source(TEST_DATA_DIR "/userdict.utf8")};
    const auto from_sources = create_from_sources(file_source(paths.dict_path), file_source(paths.model_path), users);
    ASSERT_NE(from_sources, nullptr);
    const auto cpp_users = std::array{neo_cppjieba::DictSource::memory(user_text),
                                      neo_cppjieba::DictSource::file(TEST_DATA_DIR "/userdict.utf8")};
    const auto reference = neo_cppjieba::Jieba{neo_cppjieba::DictSource::file(paths.dict_path),
                                               neo_cppjieba::DictSource::file(paths.model_path), cpp_users};
    for (const auto sentence : sentences) {
        SCOPED_TRACE(sentence);
        EXPECT_EQ(cut_words(from_sources.get(), sentence, NEO_JIEBA_V1_MIX),
                  reference.cut_strings(sentence, neo_cppjieba::CutMode::MIX));
    }
    EXPECT_NE(cut_words(from_sources.get(), sentences[0], NEO_JIEBA_V1_MIX),
              cut_words(engine.get(), sentences[0], NEO_JIEBA_V1_MIX));
}

TEST_F(CApiTest, WrittenSnapshotLoadsLikeItsSourcesFromFileOrMemory) {
    const auto user_text = std::string{"杭研大厦\n"};
    const auto dict = file_source(paths.dict_path);
    const auto model = file_source(paths.model_path);
    const auto users = std::array{memory_source(user_text)};
    const auto from_sources = create_from_sources(dict, model, users);
    ASSERT_NE(from_sources, nullptr);

    const auto directory = create_temp_directory("neo-jieba-c-snapshot-");
    const auto path = path_to_utf8(directory / "jieba.snapshot");
    ASSERT_EQ(neo_jieba_v1_write_dict_snapshot(&dict, users.data(), users.size(), path.data(), path.size(), &error),
              NEO_JIEBA_V1_OK)
        << error_message();
    const auto from_file = create_from_snapshot(file_source(path), model);
    const auto bytes = read_text(path);
    std::filesystem::remove_all(directory);
    const auto from_memory = create_from_snapshot(memory_source(bytes), model);
    ASSERT_NE(from_file, nullptr);
    ASSERT_NE(from_memory, nullptr);

    const auto expected = cut_all(from_sources.get());
    EXPECT_EQ(cut_all(from_file.get()), expected);
    EXPECT_EQ(cut_all(from_memory.get()), expected);
}

TEST_F(CApiTest, InvalidSourcesAreRejectedInEveryPositionBeforeLoading) {
    const auto dict = file_source(paths.dict_path);
    const auto model = file_source(paths.model_path);
    auto unknown_kind = dict;
    unknown_kind.kind = 0;
    auto nonzero_reserved = dict;
    nonzero_reserved.reserved = 1;
    constexpr char nul_path[] = DICT_DIR "/jieba.dict.utf8\0x";
    const auto invalid_sources = std::array{unknown_kind, nonzero_reserved, file_source(""),
                                            file_source(std::string_view{nul_path, sizeof(nul_path) - 1}),
                                            neo_jieba_source_v1{NEO_JIEBA_V1_SOURCE_MEMORY, 0, nullptr, 1}};
    for (auto index = std::size_t{0}; index < invalid_sources.size(); ++index) {
        SCOPED_TRACE(index);
        const auto &invalid = invalid_sources[index];
        neo_jieba_engine_v1 *result = nullptr;
        EXPECT_EQ(neo_jieba_v1_engine_create_from_sources(&invalid, &model, nullptr, 0, &result, &error),
                  NEO_JIEBA_V1_INVALID_ARGUMENT);
        EXPECT_EQ(neo_jieba_v1_engine_create_from_sources(&dict, &invalid, nullptr, 0, &result, &error),
                  NEO_JIEBA_V1_INVALID_ARGUMENT);
        EXPECT_EQ(neo_jieba_v1_engine_create_from_sources(&dict, &model, &invalid, 1, &result, &error),
                  NEO_JIEBA_V1_INVALID_ARGUMENT);
        EXPECT_EQ(neo_jieba_v1_engine_create_from_snapshot(&invalid, &model, &result, &error),
                  NEO_JIEBA_V1_INVALID_ARGUMENT);
        EXPECT_EQ(neo_jieba_v1_write_dict_snapshot(&invalid, nullptr, 0, "x", 1, &error),
                  NEO_JIEBA_V1_INVALID_ARGUMENT);
        EXPECT_EQ(result, nullptr);
    }
}

TEST_F(CApiTest, MissingSourceArgumentsAreRejected) {
    const auto dict = file_source(paths.dict_path);
    const auto model = file_source(paths.model_path);
    neo_jieba_engine_v1 *result = nullptr;
    EXPECT_EQ(neo_jieba_v1_engine_create_from_sources(nullptr, &model, nullptr, 0, &result, &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_engine_create_from_sources(&dict, nullptr, nullptr, 0, &result, &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_engine_create_from_sources(&dict, &model, nullptr, 1, &result, &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_engine_create_from_sources(&dict, &model, nullptr, 0, nullptr, &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_engine_create_from_snapshot(nullptr, &model, &result, &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_engine_create_from_snapshot(&dict, &model, nullptr, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(result, nullptr);
}

TEST_F(CApiTest, SnapshotWriteRejectsInvalidPaths) {
    const auto dict = file_source(paths.dict_path);
    constexpr char nul_path[] = "jieba\0.snapshot";
    EXPECT_EQ(neo_jieba_v1_write_dict_snapshot(&dict, nullptr, 0, nullptr, 0, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_write_dict_snapshot(&dict, nullptr, 0, "", 0, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_write_dict_snapshot(&dict, nullptr, 0, nul_path, sizeof(nul_path) - 1, &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_write_dict_snapshot(&dict, nullptr, 1, "x", 1, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
}

TEST_F(CApiTest, MissingSourceFileIsNamedInTheError) {
    const auto dict = file_source(TEST_DATA_DIR "/c_api_nonexistent_file");
    const auto model = file_source(paths.model_path);
    neo_jieba_engine_v1 *result = nullptr;
    EXPECT_EQ(neo_jieba_v1_engine_create_from_sources(&dict, &model, nullptr, 0, &result, &error),
              NEO_JIEBA_V1_OPERATION_FAILED);
    EXPECT_EQ(result, nullptr);
    EXPECT_NE(error_message().find("c_api_nonexistent_file"), std::string_view::npos) << error_message();
}

TEST_F(CApiTest, MalformedMemorySourceIsNamedByItsRole) {
    const auto dict = file_source(paths.dict_path);
    const auto model = memory_source("not an hmm model");
    neo_jieba_engine_v1 *result = nullptr;
    EXPECT_EQ(neo_jieba_v1_engine_create_from_sources(&dict, &model, nullptr, 0, &result, &error),
              NEO_JIEBA_V1_OPERATION_FAILED);
    EXPECT_EQ(result, nullptr);
    EXPECT_NE(error_message().find("<memory:model>"), std::string_view::npos) << error_message();
}

TEST_F(CApiTest, CorruptSnapshotIsRejected) {
    const auto snapshot = memory_source("not a dictionary snapshot");
    const auto model = file_source(paths.model_path);
    neo_jieba_engine_v1 *result = nullptr;
    EXPECT_EQ(neo_jieba_v1_engine_create_from_snapshot(&snapshot, &model, &result, &error),
              NEO_JIEBA_V1_OPERATION_FAILED);
    EXPECT_EQ(result, nullptr);
    EXPECT_GT(error.message_size, 0);
}

TEST_F(CApiTest, SnapshotWriteIntoMissingDirectoryFails) {
    const auto dict = memory_source("中国 3 ns\n");
    const auto path = std::string_view{TEST_DATA_DIR "/c_api_missing_directory/jieba.snapshot"};
    EXPECT_EQ(neo_jieba_v1_write_dict_snapshot(&dict, nullptr, 0, path.data(), path.size(), &error),
              NEO_JIEBA_V1_OPERATION_FAILED);
    EXPECT_GT(error.message_size, 0);
    EXPECT_FALSE(std::filesystem::exists(TEST_DATA_DIR "/c_api_missing_directory"));
}

TEST_F(CApiTest, TaggedCutIntoTagsMixWordsLikeCppJieba) {
    const auto workspace = make_workspace();
    const auto ranges = make_ranges();
    const auto input = std::string_view{"我来到北京清华大学"};
    ASSERT_EQ(neo_jieba_v1_cut_utf8_tagged_into(engine.get(), input.data(), input.size(), NEO_JIEBA_V1_MIX,
                                                workspace.get(), ranges.get(), &error),
              NEO_JIEBA_V1_OK)
        << error_message();
    EXPECT_EQ(tagged_words(input, borrow_ranges(ranges)),
              (std::vector<std::string>{"我/r", "来到/v", "北京/ns", "清华大学/nt"}));
}

TEST_F(CApiTest, TaggedCutIntoKeepsRangesAndTagsEachWordInEveryMode) {
    const auto reference = neo_cppjieba::Jieba{paths.dict_path, paths.model_path, paths.user_dict_path};
    const auto workspace = make_workspace();
    const auto tagged_ranges = make_ranges();
    const auto plain_ranges = make_ranges();
    constexpr char text[] = "他来到了网易杭研大厦\0iPhone6 去医院做B超，编号123";
    const auto input = std::string_view{text, sizeof(text) - 1};
    for (const auto mode : all_modes) {
        SCOPED_TRACE(mode);
        ASSERT_EQ(neo_jieba_v1_cut_utf8_tagged_into(engine.get(), input.data(), input.size(), mode, workspace.get(),
                                                    tagged_ranges.get(), &error),
                  NEO_JIEBA_V1_OK);
        ASSERT_EQ(neo_jieba_v1_cut_utf8_into(engine.get(), input.data(), input.size(), mode, workspace.get(),
                                             plain_ranges.get(), &error),
                  NEO_JIEBA_V1_OK);
        const auto tagged = borrow_ranges(tagged_ranges);
        const auto plain = borrow_ranges(plain_ranges);
        ASSERT_EQ(tagged.count, plain.count);
        ASSERT_NE(tagged.tags, nullptr);
        for (uint64_t index = 0; index < tagged.count; ++index) {
            const auto &range = tagged.ranges[index];
            EXPECT_EQ(range.byte_offset, plain.ranges[index].byte_offset);
            EXPECT_EQ(range.byte_length, plain.ranges[index].byte_length);
            const auto expected = reference.lookup_tag(input.substr(range.byte_offset, range.byte_length));
            EXPECT_EQ(tag_text(tagged.tags[index]), expected.to_string_view());
        }
    }
}

TEST_F(CApiTest, ConcurrentTaggedCutsWithSeparateHandlesMatchSerialTaggedCuts) {
    const auto *shared = engine.get();
    const auto expected = fill_all(shared, neo_jieba_v1_cut_utf8_tagged_into, tagged_words);
    ASSERT_TRUE(std::ranges::none_of(expected, [](const auto &tokens) { return tokens.empty(); }));

    for (const auto &result :
         run_workers([shared] { return fill_all(shared, neo_jieba_v1_cut_utf8_tagged_into, tagged_words); })) {
        EXPECT_EQ(result, expected);
    }
}

TEST_F(CApiTest, UntaggedFillClearsTagsOfAnEarlierTaggedFill) {
    const auto workspace = make_workspace();
    const auto ranges = make_ranges();
    const auto input = std::string_view{"中国科学院"};
    ASSERT_EQ(neo_jieba_v1_cut_utf8_tagged_into(engine.get(), input.data(), input.size(), NEO_JIEBA_V1_MIX,
                                                workspace.get(), ranges.get(), &error),
              NEO_JIEBA_V1_OK);
    ASSERT_NE(borrow_ranges(ranges).tags, nullptr);
    ASSERT_EQ(neo_jieba_v1_cut_utf8_into(engine.get(), input.data(), input.size(), NEO_JIEBA_V1_MIX, workspace.get(),
                                         ranges.get(), &error),
              NEO_JIEBA_V1_OK);
    const auto data = borrow_ranges(ranges);
    EXPECT_EQ(data.count, 1);
    EXPECT_EQ(data.tags, nullptr);
}

TEST_F(CApiTest, FailedTaggedCutIntoLeavesBothArraysEmpty) {
    const auto workspace = make_workspace();
    const auto ranges = make_ranges();
    const auto valid = std::string_view{"中国科学院"};
    ASSERT_EQ(neo_jieba_v1_cut_utf8_tagged_into(engine.get(), valid.data(), valid.size(), NEO_JIEBA_V1_MIX,
                                                workspace.get(), ranges.get(), &error),
              NEO_JIEBA_V1_OK);
    const auto invalid = std::string_view{"中国\xE4\xB8"};
    EXPECT_EQ(neo_jieba_v1_cut_utf8_tagged_into(engine.get(), invalid.data(), invalid.size(), NEO_JIEBA_V1_MIX,
                                                workspace.get(), ranges.get(), &error),
              NEO_JIEBA_V1_OPERATION_FAILED);
    const auto data = borrow_ranges(ranges);
    EXPECT_EQ(data.ranges, nullptr);
    EXPECT_EQ(data.count, 0);
    EXPECT_EQ(data.tags, nullptr);
}

TEST_F(CApiTest, LookupTagUsesDictionaryTagsThenCppJiebaRule) {
    struct TagCase {
        std::string_view word;
        std::string_view tag;
    };
    for (const auto &[word, expected] : {TagCase{"拖拉机", "n"}, TagCase{"iPhone6", "eng"}, TagCase{"12345678", "m"},
                                         TagCase{"白富", "x"}, TagCase{"", "x"}}) {
        SCOPED_TRACE(word);
        auto tag = neo_jieba_pos_tag_v1{};
        ASSERT_EQ(neo_jieba_v1_lookup_tag(engine.get(), word.data(), word.size(), &tag, &error), NEO_JIEBA_V1_OK);
        EXPECT_EQ(tag_text(tag), expected);
    }
}

TEST_F(CApiTest, LookupTagAcceptsNullEmptyWord) {
    auto tag = neo_jieba_pos_tag_v1{};
    ASSERT_EQ(neo_jieba_v1_lookup_tag(engine.get(), nullptr, 0, &tag, &error), NEO_JIEBA_V1_OK);
    EXPECT_EQ(tag_text(tag), "x");
}

TEST_F(CApiTest, LookupTagRejectsMissingArgumentsAndOversizeWords) {
    auto tag = neo_jieba_pos_tag_v1{{'s', 't', 'a', 'l'}};
    EXPECT_EQ(neo_jieba_v1_lookup_tag(nullptr, "x", 1, &tag, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(tag_text(tag), "");
    EXPECT_EQ(neo_jieba_v1_lookup_tag(engine.get(), nullptr, 1, &tag, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_lookup_tag(engine.get(), "x", 1, nullptr, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_lookup_tag(engine.get(), "x", neo_jieba_v1_max_input_bytes() + 1, &tag, &error),
              NEO_JIEBA_V1_LIMIT_EXCEEDED);
}

TEST_F(CApiTest, InvalidUtf8WordFailsAndZeroesTheTag) {
    auto tag = neo_jieba_pos_tag_v1{{'s', 't', 'a', 'l'}};
    const auto word = std::string_view{"中\xE4\xB8"};
    EXPECT_EQ(neo_jieba_v1_lookup_tag(engine.get(), word.data(), word.size(), &tag, &error),
              NEO_JIEBA_V1_OPERATION_FAILED);
    EXPECT_EQ(tag_text(tag), "");
    EXPECT_GT(error.message_size, 0);
}

TEST_F(CApiTest, AddedWordIsSegmentedAndTaggedOnlyInTheEditedClone) {
    const auto edited = make_clone();
    ASSERT_NE(edited, nullptr);
    const auto word = std::string_view{"杭研大厦"};
    const auto tag = c_tag("nz");
    ASSERT_EQ(neo_jieba_v1_engine_add_word(edited.get(), word.data(), word.size(), &tag, &error), NEO_JIEBA_V1_OK)
        << error_message();
    const auto sentence = std::string_view{"他来到了网易杭研大厦"};
    EXPECT_EQ(cut_words(edited.get(), sentence, NEO_JIEBA_V1_MIX),
              (std::vector<std::string>{"他", "来到", "了", "网易", "杭研大厦"}));
    EXPECT_EQ(cut_words(engine.get(), sentence, NEO_JIEBA_V1_MIX),
              (std::vector<std::string>{"他", "来到", "了", "网易", "杭研", "大厦"}));
    auto found = neo_jieba_pos_tag_v1{};
    ASSERT_EQ(neo_jieba_v1_lookup_tag(edited.get(), word.data(), word.size(), &found, &error), NEO_JIEBA_V1_OK);
    EXPECT_EQ(tag_text(found), "nz");
}

TEST_F(CApiTest, EditsMatchTheSameEditsThroughCpp) {
    auto reference = neo_cppjieba::Jieba{paths.dict_path, paths.model_path, paths.user_dict_path};
    const auto edited = make_clone();
    ASSERT_NE(edited, nullptr);
    const auto added = std::string_view{"杭研大厦"};
    const auto weighted = std::string_view{"京都大学深造"};
    const auto removed_word = std::string_view{"计算所"};
    const auto noun = c_tag("nz");
    const auto untagged = c_tag("");
    ASSERT_EQ(neo_jieba_v1_engine_add_word(edited.get(), added.data(), added.size(), &noun, &error), NEO_JIEBA_V1_OK);
    ASSERT_EQ(
        neo_jieba_v1_engine_add_word_with_freq(edited.get(), weighted.data(), weighted.size(), 3, &untagged, &error),
        NEO_JIEBA_V1_OK);
    auto removed = uint32_t{0};
    ASSERT_EQ(neo_jieba_v1_engine_remove_word(edited.get(), removed_word.data(), removed_word.size(), &removed, &error),
              NEO_JIEBA_V1_OK);
    EXPECT_EQ(removed, 1U);
    ASSERT_EQ(neo_jieba_v1_engine_compact(edited.get(), &error), NEO_JIEBA_V1_OK);

    reference.add_word(added, neo_cppjieba::PosTag{"nz"});
    reference.add_word_with_freq(weighted, 3, neo_cppjieba::PosTag{});
    reference.remove_word(removed_word);
    reference.compact();
    for (const auto sentence : sentences) {
        SCOPED_TRACE(sentence);
        EXPECT_EQ(cut_words(edited.get(), sentence, NEO_JIEBA_V1_MIX),
                  reference.cut_strings(sentence, neo_cppjieba::CutMode::MIX));
    }
    for (const auto word : {added, weighted, removed_word}) {
        SCOPED_TRACE(word);
        auto found = neo_jieba_pos_tag_v1{};
        ASSERT_EQ(neo_jieba_v1_lookup_tag(edited.get(), word.data(), word.size(), &found, &error), NEO_JIEBA_V1_OK);
        const auto expected = reference.lookup_tag(word);
        EXPECT_EQ(tag_text(found), expected.to_string_view());
    }
}

TEST_F(CApiTest, RemoveWordReportsWhetherTheWordWasPresent) {
    const auto edited = make_clone();
    ASSERT_NE(edited, nullptr);
    const auto word = std::string_view{"清华大学"};
    auto removed = uint32_t{7};
    ASSERT_EQ(neo_jieba_v1_engine_remove_word(edited.get(), word.data(), word.size(), &removed, &error),
              NEO_JIEBA_V1_OK);
    EXPECT_EQ(removed, 1U);
    ASSERT_EQ(neo_jieba_v1_engine_remove_word(edited.get(), word.data(), word.size(), &removed, &error),
              NEO_JIEBA_V1_OK);
    EXPECT_EQ(removed, 0U);
    const auto words = cut_words(edited.get(), "我来到北京清华大学", NEO_JIEBA_V1_MIX);
    EXPECT_EQ(std::ranges::find(words, word), words.end());
}

TEST_F(CApiTest, CompactKeepsEveryCutResult) {
    const auto edited = make_clone();
    ASSERT_NE(edited, nullptr);
    const auto added = std::string_view{"杭研大厦"};
    const auto removed_word = std::string_view{"清华大学"};
    const auto tag = c_tag("nz");
    auto removed = uint32_t{0};
    ASSERT_EQ(neo_jieba_v1_engine_add_word(edited.get(), added.data(), added.size(), &tag, &error), NEO_JIEBA_V1_OK);
    ASSERT_EQ(neo_jieba_v1_engine_remove_word(edited.get(), removed_word.data(), removed_word.size(), &removed, &error),
              NEO_JIEBA_V1_OK);
    const auto before = cut_all(edited.get());
    ASSERT_EQ(neo_jieba_v1_engine_compact(edited.get(), &error), NEO_JIEBA_V1_OK);
    EXPECT_EQ(cut_all(edited.get()), before);
}

TEST_F(CApiTest, CloneSurvivesTheEngineItWasCopiedFrom) {
    auto first = make_clone();
    ASSERT_NE(first, nullptr);
    neo_jieba_engine_v1 *created = nullptr;
    ASSERT_EQ(neo_jieba_v1_engine_clone(first.get(), &created, &error), NEO_JIEBA_V1_OK);
    const auto second = EngineOwner{created, neo_jieba_v1_engine_destroy};
    first.reset();
    EXPECT_EQ(cut_all(second.get()), cut_all(engine.get()));
}

TEST_F(CApiTest, FourByteTagsRoundTripWithoutTerminator) {
    const auto edited = make_clone();
    ASSERT_NE(edited, nullptr);
    const auto word = std::string_view{"杭研大厦"};
    const auto tag = c_tag("abcd");
    ASSERT_EQ(neo_jieba_v1_engine_add_word(edited.get(), word.data(), word.size(), &tag, &error), NEO_JIEBA_V1_OK);
    auto found = neo_jieba_pos_tag_v1{};
    ASSERT_EQ(neo_jieba_v1_lookup_tag(edited.get(), word.data(), word.size(), &found, &error), NEO_JIEBA_V1_OK);
    EXPECT_EQ(tag_text(found), "abcd");
}

TEST_F(CApiTest, EditArgumentsAreRejectedBeforeTheDictionaryChanges) {
    const auto edited = make_clone();
    ASSERT_NE(edited, nullptr);
    auto *const target = edited.get();
    const auto word = std::string_view{"杭研大厦"};
    const auto tag = c_tag("n");
    auto unpadded = c_tag("n");
    unpadded.data[2] = 'x';
    EXPECT_EQ(neo_jieba_v1_engine_add_word(nullptr, word.data(), word.size(), &tag, &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_engine_add_word(target, nullptr, 1, &tag, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_engine_add_word(target, word.data(), 0, &tag, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_engine_add_word(target, word.data(), word.size(), nullptr, &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_engine_add_word(target, word.data(), word.size(), &unpadded, &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_engine_add_word(target, word.data(), neo_jieba_v1_max_input_bytes() + 1, &tag, &error),
              NEO_JIEBA_V1_LIMIT_EXCEEDED);
    EXPECT_EQ(neo_jieba_v1_engine_add_word_with_freq(target, word.data(), word.size(), 0, &tag, &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_engine_add_word_with_freq(target, word.data(), word.size(), -1, &tag, &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_engine_remove_word(target, word.data(), word.size(), nullptr, &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    auto removed = uint32_t{1};
    EXPECT_EQ(neo_jieba_v1_engine_remove_word(target, word.data(), 0, &removed, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(removed, 0U);
    EXPECT_EQ(neo_jieba_v1_engine_compact(nullptr, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    neo_jieba_engine_v1 *copy = nullptr;
    EXPECT_EQ(neo_jieba_v1_engine_clone(nullptr, &copy, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(copy, nullptr);
    EXPECT_EQ(neo_jieba_v1_engine_clone(engine.get(), nullptr, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);

    const auto sentence = std::string_view{"他来到了网易杭研大厦"};
    EXPECT_EQ(cut_words(target, sentence, NEO_JIEBA_V1_MIX), cut_words(engine.get(), sentence, NEO_JIEBA_V1_MIX));
}

TEST_F(CApiTest, InvalidUtf8EditsFailWithoutChangingTheDictionary) {
    const auto edited = make_clone();
    ASSERT_NE(edited, nullptr);
    const auto tag = c_tag("n");
    const auto invalid = std::string_view{"中\xE4\xB8"};
    EXPECT_EQ(neo_jieba_v1_engine_add_word(edited.get(), invalid.data(), invalid.size(), &tag, &error),
              NEO_JIEBA_V1_OPERATION_FAILED);
    EXPECT_GT(error.message_size, 0);
    auto removed = uint32_t{1};
    EXPECT_EQ(neo_jieba_v1_engine_remove_word(edited.get(), invalid.data(), invalid.size(), &removed, &error),
              NEO_JIEBA_V1_OPERATION_FAILED);
    EXPECT_EQ(removed, 0U);
    EXPECT_EQ(cut_all(edited.get()), cut_all(engine.get()));
}

TEST_F(CApiTest, ReadersOfAnEngineRunWhileItsCloneIsMadeAndEdited) {
    const auto *shared = engine.get();
    const auto expected = cut_all(shared);
    auto edited = EngineOwner{nullptr, neo_jieba_v1_engine_destroy};
    {
        // Cloning only reads the shared engine; every edit goes to the private copy.
        auto writer = std::jthread{[shared, &edited] {
            neo_jieba_engine_v1 *created = nullptr;
            neo_jieba_v1_engine_clone(shared, &created, nullptr);
            edited.reset(created);
            const auto word = std::string_view{"杭研大厦"};
            const auto tag = c_tag("nz");
            for (auto round = std::size_t{0}; round < rounds; ++round) {
                auto removed = uint32_t{0};
                neo_jieba_v1_engine_add_word(created, word.data(), word.size(), &tag, nullptr);
                neo_jieba_v1_engine_remove_word(created, word.data(), word.size(), &removed, nullptr);
            }
            neo_jieba_v1_engine_compact(created, nullptr);
        }};
        for (const auto &result : run_workers([shared] { return cut_all(shared); })) {
            EXPECT_EQ(result, expected);
        }
    }
    EXPECT_NE(edited, nullptr);
}

TEST_F(CApiTest, TfIdfKeywordsMatchCppExtraction) {
    const auto workspace = make_workspace();
    const auto keywords = make_keywords();
    ASSERT_EQ(neo_jieba_v1_tfidf_extract_into(tfidf.get(), engine.get(), keyword_text.data(), keyword_text.size(), 5,
                                              &unfiltered_tfidf_options, workspace.get(), keywords.get(), &error),
              NEO_JIEBA_V1_OK)
        << error_message();
    const auto expected = reference_tfidf().extract(reference_jieba(), keyword_text, 5, neo_cppjieba::TfIdfOptions{});
    ASSERT_EQ(expected.size(), 5U);
    EXPECT_EQ(summarize(keyword_text, borrow_keywords(keywords)), summarize(expected));
}

TEST_F(CApiTest, TextRankKeywordsMatchCppExtraction) {
    const auto workspace = make_workspace();
    const auto keywords = make_keywords();
    ASSERT_EQ(neo_jieba_v1_textrank_extract_into(textrank.get(), engine.get(), keyword_text.data(), keyword_text.size(),
                                                 5, &default_textrank_options, workspace.get(), keywords.get(), &error),
              NEO_JIEBA_V1_OK)
        << error_message();
    const auto expected =
        reference_textrank().extract(reference_jieba(), keyword_text, 5, neo_cppjieba::TextRankOptions{});
    ASSERT_FALSE(expected.empty());
    EXPECT_EQ(summarize(keyword_text, borrow_keywords(keywords)), summarize(expected));
}

TEST_F(CApiTest, AllowedTagsFilterBothRankersLikeCpp) {
    const auto workspace = make_workspace();
    const auto keywords = make_keywords();
    const auto allowed = std::array{c_tag("ns"), c_tag("n"), c_tag("vn"), c_tag("v")};
    const auto cpp_allowed = std::vector<neo_cppjieba::PosTag>(neo_cppjieba::python_textrank_tags.begin(),
                                                               neo_cppjieba::python_textrank_tags.end());
    const auto tfidf_options = neo_jieba_tfidf_options_v1{allowed.data(), allowed.size()};
    ASSERT_EQ(neo_jieba_v1_tfidf_extract_into(tfidf.get(), engine.get(), keyword_text.data(), keyword_text.size(), 10,
                                              &tfidf_options, workspace.get(), keywords.get(), &error),
              NEO_JIEBA_V1_OK);
    const auto tfidf_result = summarize(keyword_text, borrow_keywords(keywords));
    EXPECT_EQ(tfidf_result, summarize(reference_tfidf().extract(reference_jieba(), keyword_text, 10,
                                                                neo_cppjieba::TfIdfOptions{cpp_allowed})));

    const auto textrank_options = neo_jieba_textrank_options_v1{5, 10, 0.85, allowed.data(), allowed.size()};
    ASSERT_EQ(neo_jieba_v1_textrank_extract_into(textrank.get(), engine.get(), keyword_text.data(), keyword_text.size(),
                                                 10, &textrank_options, workspace.get(), keywords.get(), &error),
              NEO_JIEBA_V1_OK);
    EXPECT_EQ(summarize(keyword_text, borrow_keywords(keywords)),
              summarize(reference_textrank().extract(reference_jieba(), keyword_text, 10,
                                                     neo_cppjieba::TextRankOptions{5, 10, 0.85, cpp_allowed})));

    ASSERT_FALSE(tfidf_result.empty());
    const auto allowed_text = std::array<std::string_view, 4>{"ns", "n", "vn", "v"};
    for (const auto &keyword : tfidf_result) {
        EXPECT_NE(std::ranges::find(allowed_text, keyword.tag), allowed_text.end()) << keyword.word;
    }
}

TEST_F(CApiTest, RepeatedKeywordListsEveryOccurrenceInInputOrder) {
    const auto workspace = make_workspace();
    const auto keywords = make_keywords();
    const auto input = std::string_view{"我爱苹果，苹果很甜，我每天吃苹果。"};
    ASSERT_EQ(neo_jieba_v1_tfidf_extract_into(tfidf.get(), engine.get(), input.data(), input.size(), 10,
                                              &unfiltered_tfidf_options, workspace.get(), keywords.get(), &error),
              NEO_JIEBA_V1_OK);
    const auto summaries = summarize(input, borrow_keywords(keywords));
    const auto repeated = std::ranges::find(summaries, std::string{"苹果"}, &KeywordSummary::word);
    ASSERT_NE(repeated, summaries.end());
    ASSERT_EQ(repeated->occurrences.size(), 3U);
    EXPECT_TRUE(std::ranges::is_sorted(repeated->occurrences));
    for (const auto &[offset, length] : repeated->occurrences) {
        EXPECT_EQ(input.substr(offset, length), repeated->word);
    }
}

TEST_F(CApiTest, TopNLimitsTheKeywordCount) {
    const auto workspace = make_workspace();
    const auto keywords = make_keywords();
    for (const auto top_n : {uint64_t{0}, uint64_t{2}}) {
        SCOPED_TRACE(top_n);
        ASSERT_EQ(neo_jieba_v1_tfidf_extract_into(tfidf.get(), engine.get(), keyword_text.data(), keyword_text.size(),
                                                  top_n, &unfiltered_tfidf_options, workspace.get(), keywords.get(),
                                                  &error),
                  NEO_JIEBA_V1_OK);
        EXPECT_EQ(borrow_keywords(keywords).count, top_n);
    }
    ASSERT_EQ(neo_jieba_v1_tfidf_extract_into(tfidf.get(), engine.get(), keyword_text.data(), keyword_text.size(),
                                              std::numeric_limits<uint64_t>::max(), &unfiltered_tfidf_options,
                                              workspace.get(), keywords.get(), &error),
              NEO_JIEBA_V1_OK);
    const auto every = reference_tfidf().extract(reference_jieba(), keyword_text,
                                                 std::numeric_limits<std::size_t>::max(), neo_cppjieba::TfIdfOptions{});
    EXPECT_GT(every.size(), 5U);
    EXPECT_EQ(summarize(keyword_text, borrow_keywords(keywords)), summarize(every));
}

TEST_F(CApiTest, MemoryKeywordSourcesMatchFiles) {
    const auto idf_text = read_text(idf_path);
    const auto stop_words_text = read_text(stop_words_path);
    const auto idf = memory_source(idf_text);
    const auto stop_words = memory_source(stop_words_text);
    neo_jieba_tfidf_v1 *created = nullptr;
    ASSERT_EQ(neo_jieba_v1_tfidf_create(&idf, &stop_words, &created, &error), NEO_JIEBA_V1_OK) << error_message();
    const auto from_memory = TfIdfOwner{created, neo_jieba_v1_tfidf_destroy};
    const auto workspace = make_workspace();
    const auto keywords = make_keywords();
    for (const auto *extractor : {from_memory.get(), tfidf.get()}) {
        ASSERT_EQ(neo_jieba_v1_tfidf_extract_into(extractor, engine.get(), keyword_text.data(), keyword_text.size(), 5,
                                                  &unfiltered_tfidf_options, workspace.get(), keywords.get(), &error),
                  NEO_JIEBA_V1_OK);
        EXPECT_EQ(
            summarize(keyword_text, borrow_keywords(keywords)),
            summarize(reference_tfidf().extract(reference_jieba(), keyword_text, 5, neo_cppjieba::TfIdfOptions{})));
    }
}

TEST_F(CApiTest, KeywordsSurviveTheirExtractorWorkspaceAndEngine) {
    const auto keywords = make_keywords();
    {
        const auto local_engine = make_clone();
        const auto workspace = make_workspace();
        const auto stop_words = file_source(stop_words_path);
        neo_jieba_textrank_v1 *created = nullptr;
        ASSERT_EQ(neo_jieba_v1_textrank_create(&stop_words, &created, &error), NEO_JIEBA_V1_OK);
        const auto extractor = TextRankOwner{created, neo_jieba_v1_textrank_destroy};
        ASSERT_EQ(neo_jieba_v1_textrank_extract_into(extractor.get(), local_engine.get(), keyword_text.data(),
                                                     keyword_text.size(), 5, &default_textrank_options, workspace.get(),
                                                     keywords.get(), &error),
                  NEO_JIEBA_V1_OK);
    }
    const auto expected =
        reference_textrank().extract(reference_jieba(), keyword_text, 5, neo_cppjieba::TextRankOptions{});
    EXPECT_EQ(summarize(keyword_text, borrow_keywords(keywords)), summarize(expected));
}

TEST_F(CApiTest, FailedExtractionLeavesKeywordsEmpty) {
    const auto workspace = make_workspace();
    const auto keywords = make_keywords();
    ASSERT_EQ(neo_jieba_v1_tfidf_extract_into(tfidf.get(), engine.get(), keyword_text.data(), keyword_text.size(), 5,
                                              &unfiltered_tfidf_options, workspace.get(), keywords.get(), &error),
              NEO_JIEBA_V1_OK);
    const auto invalid = std::string_view{"中国\xE4\xB8"};
    EXPECT_EQ(neo_jieba_v1_tfidf_extract_into(tfidf.get(), engine.get(), invalid.data(), invalid.size(), 5,
                                              &unfiltered_tfidf_options, workspace.get(), keywords.get(), &error),
              NEO_JIEBA_V1_OPERATION_FAILED);
    EXPECT_GT(error.message_size, 0);
    const auto data = borrow_keywords(keywords);
    EXPECT_EQ(data.keywords, nullptr);
    EXPECT_EQ(data.count, 0);
    EXPECT_EQ(data.occurrences, nullptr);
    EXPECT_EQ(data.occurrence_count, 0);
}

TEST_F(CApiTest, KeywordExtractionRejectsMissingHandlesAndInput) {
    const auto workspace = make_workspace();
    const auto keywords = make_keywords();
    const auto *text = keyword_text.data();
    const auto size = uint64_t{keyword_text.size()};
    const auto *options = &unfiltered_tfidf_options;
    ASSERT_EQ(neo_jieba_v1_tfidf_extract_into(tfidf.get(), engine.get(), text, size, 5, options, workspace.get(),
                                              keywords.get(), &error),
              NEO_JIEBA_V1_OK);
    EXPECT_EQ(neo_jieba_v1_tfidf_extract_into(nullptr, engine.get(), text, size, 5, options, workspace.get(),
                                              keywords.get(), &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(borrow_keywords(keywords).count, 0);
    EXPECT_EQ(neo_jieba_v1_tfidf_extract_into(tfidf.get(), nullptr, text, size, 5, options, workspace.get(),
                                              keywords.get(), &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_tfidf_extract_into(tfidf.get(), engine.get(), nullptr, 1, 5, options, workspace.get(),
                                              keywords.get(), &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_tfidf_extract_into(tfidf.get(), engine.get(), text, size, 5, options, nullptr,
                                              keywords.get(), &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_tfidf_extract_into(tfidf.get(), engine.get(), text, size, 5, options, workspace.get(),
                                              nullptr, &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_tfidf_extract_into(tfidf.get(), engine.get(), text, neo_jieba_v1_max_input_bytes() + 1, 5,
                                              options, workspace.get(), keywords.get(), &error),
              NEO_JIEBA_V1_LIMIT_EXCEEDED);
}

TEST_F(CApiTest, KeywordOptionsAreValidatedBeforeExtraction) {
    const auto workspace = make_workspace();
    const auto keywords = make_keywords();
    const auto *text = keyword_text.data();
    const auto size = uint64_t{keyword_text.size()};
    auto unpadded = c_tag("n");
    unpadded.data[2] = 'x';
    const auto bad_tags = neo_jieba_tfidf_options_v1{&unpadded, 1};
    const auto missing_tags = neo_jieba_tfidf_options_v1{nullptr, 1};
    for (const auto *options : {&bad_tags, &missing_tags, static_cast<const neo_jieba_tfidf_options_v1 *>(nullptr)}) {
        EXPECT_EQ(neo_jieba_v1_tfidf_extract_into(tfidf.get(), engine.get(), text, size, 5, options, workspace.get(),
                                                  keywords.get(), &error),
                  NEO_JIEBA_V1_INVALID_ARGUMENT);
    }
    for (const auto damping : {-0.1, 1.5, std::numeric_limits<double>::quiet_NaN()}) {
        SCOPED_TRACE(damping);
        const auto options = neo_jieba_textrank_options_v1{5, 10, damping, nullptr, 0};
        EXPECT_EQ(neo_jieba_v1_textrank_extract_into(textrank.get(), engine.get(), text, size, 5, &options,
                                                     workspace.get(), keywords.get(), &error),
                  NEO_JIEBA_V1_INVALID_ARGUMENT);
    }
    const auto unpadded_textrank = neo_jieba_textrank_options_v1{5, 10, 0.85, &unpadded, 1};
    EXPECT_EQ(neo_jieba_v1_textrank_extract_into(textrank.get(), engine.get(), text, size, 5, &unpadded_textrank,
                                                 workspace.get(), keywords.get(), &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_textrank_extract_into(textrank.get(), engine.get(), text, size, 5, nullptr, workspace.get(),
                                                 keywords.get(), &error),
              NEO_JIEBA_V1_INVALID_ARGUMENT);
}

TEST_F(CApiTest, KeywordHandleCreationChecksArgumentsAndSources) {
    const auto idf = file_source(idf_path);
    const auto stop_words = file_source(stop_words_path);
    auto invalid = stop_words;
    invalid.reserved = 1;
    const auto missing = file_source(TEST_DATA_DIR "/c_api_nonexistent_file");
    neo_jieba_tfidf_v1 *tfidf_result = nullptr;
    neo_jieba_textrank_v1 *textrank_result = nullptr;
    EXPECT_EQ(neo_jieba_v1_tfidf_create(&idf, &invalid, &tfidf_result, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_tfidf_create(nullptr, &stop_words, &tfidf_result, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_tfidf_create(&idf, &stop_words, nullptr, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_textrank_create(&invalid, &textrank_result, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_textrank_create(&stop_words, nullptr, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(neo_jieba_v1_tfidf_create(&missing, &stop_words, &tfidf_result, &error), NEO_JIEBA_V1_OPERATION_FAILED);
    EXPECT_NE(error_message().find("c_api_nonexistent_file"), std::string_view::npos) << error_message();
    EXPECT_EQ(neo_jieba_v1_textrank_create(&missing, &textrank_result, &error), NEO_JIEBA_V1_OPERATION_FAILED);
    EXPECT_EQ(tfidf_result, nullptr);
    EXPECT_EQ(textrank_result, nullptr);

    EXPECT_EQ(neo_jieba_v1_keywords_create(nullptr, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    auto data = neo_jieba_borrowed_keywords_v1{nullptr, 3, nullptr, 4};
    EXPECT_EQ(neo_jieba_v1_keywords_borrow_data(nullptr, &data, &error), NEO_JIEBA_V1_INVALID_ARGUMENT);
    EXPECT_EQ(data.count, 0);
    EXPECT_EQ(data.occurrence_count, 0);
}

TEST_F(CApiTest, ConcurrentExtractionWithSeparateHandlesMatchesSerialExtraction) {
    const auto *shared_engine = engine.get();
    const auto *shared_tfidf = tfidf.get();
    const auto *shared_textrank = textrank.get();
    const auto expected = extract_all(shared_engine, shared_tfidf, shared_textrank);
    ASSERT_TRUE(std::ranges::none_of(expected, [](const auto &keywords) { return keywords.empty(); }));

    for (const auto &result : run_workers([shared_engine, shared_tfidf, shared_textrank] {
             return extract_all(shared_engine, shared_tfidf, shared_textrank);
         })) {
        EXPECT_EQ(result, expected);
    }
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
    neo_jieba_v1_workspace_destroy(nullptr);
    neo_jieba_v1_ranges_destroy(nullptr);
    neo_jieba_v1_tfidf_destroy(nullptr);
    neo_jieba_v1_textrank_destroy(nullptr);
    neo_jieba_v1_keywords_destroy(nullptr);
}

} // namespace
