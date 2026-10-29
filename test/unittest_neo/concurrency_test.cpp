#include "gtest/gtest.h"
#include "neo/Jieba.hpp"
#include "neo/KeywordExtractor.hpp"

#include "test_paths.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

using namespace neo_cppjieba;

inline constexpr auto DICT_FILE = std::string_view{DICT_DIR "/jieba.dict.utf8"};
inline constexpr auto HMM_MODEL_FILE = std::string_view{DICT_DIR "/hmm_model.utf8"};
inline constexpr auto IDF_FILE = std::string_view{DICT_DIR "/idf.utf8"};
inline constexpr auto STOP_WORDS_FILE = std::string_view{DICT_DIR "/stop_words.utf8"};

namespace {

// Enough workers and repetitions for ThreadSanitizer to observe overlapping calls.
constexpr auto worker_count = std::size_t{4};
constexpr auto rounds = std::size_t{8};
constexpr auto top_n = std::size_t{5};

constexpr auto sentences = std::array<std::string_view, 5>{"我来到北京清华大学", "他来到了网易杭研大厦",
                                                           "小明硕士毕业于中国科学院计算所，后在日本京都大学深造",
                                                           "令狐冲是云计算行业的专家", "去医院做B超，编号123"};

constexpr auto all_modes = std::array{CutMode::MIX,  CutMode::MIX_NO_HMM, CutMode::MP,           CutMode::HMM,
                                      CutMode::FULL, CutMode::SEARCH,     CutMode::SEARCH_NO_HMM};

using Words = std::vector<std::string>;

// Keyword has no equality; its word and weight identify one ranking entry.
struct RankedWord {
    std::string word;
    double weight;

    auto operator==(const RankedWord &) const -> bool = default;
};

// Share one immutable model between tests; every call below only reads it.
auto shared_jieba() -> const Jieba & {
    static const auto jieba = Jieba{DICT_FILE, HMM_MODEL_FILE, ""};
    return jieba;
}

// Cut every sentence in every mode through one reused workspace, as a single worker does.
auto cut_all(const Jieba &jieba) -> std::vector<Words> {
    auto workspace = Workspace{};
    auto results = std::vector<Words>{};
    results.reserve(all_modes.size() * sentences.size());
    for (const auto mode : all_modes) {
        for (const auto sentence : sentences) {
            jieba.cut_into(sentence, mode, results.emplace_back(), workspace);
        }
    }
    return results;
}

// Extract keywords for every sentence through one reused keyword workspace.
template <typename Extractor, typename Options>
auto extract_all(const Extractor &extractor, const Jieba &jieba, const Options &options)
    -> std::vector<std::vector<RankedWord>> {
    auto workspace = KeywordWorkspace{};
    auto keywords = std::vector<Keyword<char>>{};
    auto results = std::vector<std::vector<RankedWord>>{};
    results.reserve(sentences.size());
    for (const auto sentence : sentences) {
        extractor.extract_into(jieba, sentence, top_n, options, keywords, workspace);
        auto &ranked = results.emplace_back();
        ranked.reserve(keywords.size());
        std::ranges::transform(keywords, std::back_inserter(ranked),
                               [](const Keyword<char> &keyword) { return RankedWord{keyword.word, keyword.weight}; });
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

} // namespace

TEST(ConcurrencyTest, ConcurrentCutsWithSeparateWorkspacesMatchSerialCuts) {
    const auto &jieba = shared_jieba();
    const auto expected = cut_all(jieba);

    for (const auto &result : run_workers([&jieba] { return cut_all(jieba); })) {
        EXPECT_EQ(result, expected);
    }
}

TEST(ConcurrencyTest, ConcurrentTfIdfExtractionMatchesSerialExtraction) {
    const auto &jieba = shared_jieba();
    const auto extractor = TfIdfExtractor{IDF_FILE, STOP_WORDS_FILE};
    const auto options = TfIdfOptions{};
    const auto expected = extract_all(extractor, jieba, options);

    for (const auto &result : run_workers([&] { return extract_all(extractor, jieba, options); })) {
        EXPECT_EQ(result, expected);
    }
}

TEST(ConcurrencyTest, ConcurrentTextRankExtractionMatchesSerialExtraction) {
    const auto &jieba = shared_jieba();
    const auto extractor = TextRankExtractor{STOP_WORDS_FILE};
    const auto options = TextRankOptions{};
    const auto expected = extract_all(extractor, jieba, options);

    for (const auto &result : run_workers([&] { return extract_all(extractor, jieba, options); })) {
        EXPECT_EQ(result, expected);
    }
}

TEST(ConcurrencyTest, EditingACloneLeavesConcurrentCutsOfTheOriginalUnchanged) {
    const auto &jieba = shared_jieba();
    const auto expected = cut_all(jieba);

    auto edited = Words{};
    auto results = std::vector<std::vector<Words>>{};
    {
        // The editor copies and edits while the workers keep cutting the original.
        const auto editor = std::jthread{[&jieba, &edited] {
            auto next = jieba.clone();
            next.add_word("杭研", PosTag{"nz"});
            edited = next.cut_strings(std::string_view{"他来到了网易杭研大厦"}, CutMode::MP);
        }};
        results = run_workers([&jieba] { return cut_all(jieba); });
    }

    EXPECT_EQ(edited, (Words{"他", "来到", "了", "网易", "杭研", "大厦"}));
    for (const auto &result : results) {
        EXPECT_EQ(result, expected);
    }
}
