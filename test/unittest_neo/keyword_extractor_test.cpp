#include "gtest/gtest.h"
#include "neo/Jieba.hpp"
#include "neo/KeywordExtractor.hpp"
#include "neo/Unicode.hpp"

#include "test_paths.h"

#include <cstddef>
#include <format>
#include <algorithm>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using namespace neo_cppjieba;

namespace {

inline constexpr auto SMALL_DICT_FILE = std::string_view{TEST_DATA_DIR "/extra_dict/jieba.dict.small.utf8"};
inline constexpr auto HMM_MODEL_FILE = std::string_view{DICT_DIR "/hmm_model.utf8"};
inline constexpr auto USER_DICT_FILE = std::string_view{TEST_DATA_DIR "/userdict.utf8"};
inline constexpr auto IDF_FILE = std::string_view{DICT_DIR "/idf.utf8"};
inline constexpr auto STOP_WORDS_FILE = std::string_view{DICT_DIR "/stop_words.utf8"};

constexpr auto all = std::numeric_limits<size_t>::max();

auto small_jieba() -> const Jieba & {
    static const auto jieba = Jieba{SMALL_DICT_FILE, HMM_MODEL_FILE, ""};
    return jieba;
}

auto user_jieba() -> const Jieba & {
    static const auto jieba = Jieba{SMALL_DICT_FILE, HMM_MODEL_FILE, USER_DICT_FILE};
    return jieba;
}

// Format keywords as the legacy CppJieba tests print KeywordExtractor::Word: UTF-8 byte offsets, six digits.
auto format_keywords(const std::vector<Keyword<char>> &keywords) -> std::string {
    auto out = std::string{"["};
    for (const auto &keyword : keywords) {
        auto offsets = std::string{};
        for (const auto &position : keyword.positions) {
            offsets += std::format("{}{}", offsets.empty() ? "" : ", ", position.source.begin);
        }
        out += std::format("{}{{\"word\": \"{}\", \"offset\": [{}], \"weight\": {:.6}}}", out.size() > 1 ? ", " : "",
                           keyword.word, offsets, keyword.weight);
    }
    return out + "]";
}

} // namespace

// Expected values are CppJieba's keyword_extractor_test.cpp fixtures.
TEST(KeywordExtractorTest, TfIdfMatchesCppJiebaFixtures) {
    const auto extractor = TfIdfExtractor{small_jieba(), IDF_FILE, STOP_WORDS_FILE};
    EXPECT_EQ(format_keywords(extractor.extract(std::string_view{"你好世界世界而且而且"}, 5)),
              R"([{"word": "世界", "offset": [6, 12], "weight": 8.73506}, )"
              R"({"word": "你好", "offset": [0], "weight": 7.95788}])");
    EXPECT_EQ(
        format_keywords(extractor.extract(
            std::string_view{"我是拖拉机学院手扶拖拉机专业的。不用多久，我就会升职加薪，当上CEO，走上人生巅峰。"}, 5)),
        R"([{"word": "CEO", "offset": [93], "weight": 11.7392}, {"word": "升职", "offset": [72], "weight": 10.8562}, )"
        R"({"word": "加薪", "offset": [78], "weight": 10.6426}, )"
        R"({"word": "手扶拖拉机", "offset": [21], "weight": 10.0089}, )"
        R"({"word": "巅峰", "offset": [111], "weight": 9.49396}])");
    EXPECT_EQ(format_keywords(extractor.extract(std::string_view{"一部iPhone6"}, 5)),
              R"([{"word": "iPhone6", "offset": [6], "weight": 11.7392}, )"
              R"({"word": "一部", "offset": [0], "weight": 6.47592}])");

    const auto user = TfIdfExtractor{user_jieba(), IDF_FILE, STOP_WORDS_FILE};
    EXPECT_EQ(format_keywords(user.extract(std::string_view{"蓝翔优秀毕业生"}, 5)),
              R"([{"word": "蓝翔", "offset": [0], "weight": 11.7392}, )"
              R"({"word": "毕业生", "offset": [12], "weight": 8.13549}, )"
              R"({"word": "优秀", "offset": [6], "weight": 6.78347}])");
}

// Expected values are CppJieba's textrank_test.cpp fixtures.
TEST(KeywordExtractorTest, TextRankMatchesCppJiebaFixtures) {
    const auto extractor = TextRankExtractor{small_jieba(), STOP_WORDS_FILE};
    EXPECT_EQ(format_keywords(extractor.extract(std::string_view{"你好世界世界而且而且"}, 5)),
              R"([{"word": "世界", "offset": [6, 12], "weight": 1}, )"
              R"({"word": "你好", "offset": [0], "weight": 0.519787}])");
    EXPECT_EQ(
        format_keywords(extractor.extract(
            std::string_view{"我是拖拉机学院手扶拖拉机专业的。不用多久，我就会升职加薪，当上 CEO，走上人生巅峰"}, 5)),
        R"([{"word": "当上", "offset": [87], "weight": 1}, {"word": "不用", "offset": [48], "weight": 0.989848}, )"
        R"({"word": "多久", "offset": [54], "weight": 0.985126}, {"word": "加薪", "offset": [78], "weight": 0.983046}, )"
        R"({"word": "升职", "offset": [72], "weight": 0.980278}])");
    EXPECT_EQ(format_keywords(extractor.extract(std::string_view{"一部iPhone6"}, 5)),
              R"([{"word": "一部", "offset": [0], "weight": 1}, )"
              R"({"word": "iPhone6", "offset": [6], "weight": 0.996126}])");

    const auto user = TextRankExtractor{user_jieba(), STOP_WORDS_FILE};
    EXPECT_EQ(format_keywords(user.extract(std::string_view{"蓝翔优秀毕业生"}, 5)),
              R"([{"word": "蓝翔", "offset": [0], "weight": 1}, )"
              R"({"word": "毕业生", "offset": [12], "weight": 0.996685}, )"
              R"({"word": "优秀", "offset": [6], "weight": 0.992994}])");
}

TEST(KeywordExtractorTest, TopNAndEmptyInput) {
    const auto tfidf = TfIdfExtractor{small_jieba(), IDF_FILE, STOP_WORDS_FILE};
    const auto textrank = TextRankExtractor{small_jieba(), STOP_WORDS_FILE};
    constexpr auto text = std::string_view{"你好世界世界而且而且"};

    EXPECT_TRUE(tfidf.extract(text, 0).empty());
    EXPECT_TRUE(textrank.extract(text, 0).empty());
    EXPECT_TRUE(tfidf.extract(std::string_view{}, 5).empty());
    EXPECT_TRUE(textrank.extract(std::string_view{}, 5).empty());
    ASSERT_EQ(tfidf.extract(text, 1).size(), 1U);
    EXPECT_EQ(tfidf.extract(text, 1)[0].word, "世界");
    EXPECT_EQ(tfidf.extract(text, all).size(), 2U);

    // Only stop words and one-rune words: nothing is a candidate.
    EXPECT_TRUE(tfidf.extract(std::string_view{"的了，。"}, 5).empty());
    EXPECT_TRUE(textrank.extract(std::string_view{"的了，。"}, 5).empty());
}

// A lone candidate has no TextRank edges, so it keeps CppJieba's zero score.
TEST(KeywordExtractorTest, TextRankWithoutEdges) {
    const auto textrank = TextRankExtractor{small_jieba(), STOP_WORDS_FILE};
    const auto keywords = textrank.extract(std::string_view{"世界"}, 5);
    ASSERT_EQ(keywords.size(), 1U);
    EXPECT_EQ(keywords[0].word, "世界");
    EXPECT_EQ(keywords[0].weight, 0.0);

    const auto narrow = TextRankExtractor{small_jieba(), STOP_WORDS_FILE, {.window = 1}};
    for (const auto &keyword : narrow.extract(std::string_view{"你好世界世界而且而且"}, 5)) {
        EXPECT_EQ(keyword.weight, 0.0);
    }
}

TEST(KeywordExtractorTest, PositionsUseInputCodeUnits) {
    const auto tfidf = TfIdfExtractor{small_jieba(), IDF_FILE, STOP_WORDS_FILE};
    const auto textrank = TextRankExtractor{small_jieba(), STOP_WORDS_FILE};
    constexpr auto utf8 = std::string_view{"我是拖拉机学院手扶拖拉机专业的。不用多久，我就会升职加薪，当上CEO，走上人生巅峰。"};
    const auto runes = decode(utf8);
    const auto utf16 = encode<char16_t>(runes);
    const auto utf32 = encode<char32_t>(runes);

    const auto check_encoding = [&](const auto &extractor) {
        const auto expected = extractor.extract(utf8, all);
        const auto wide16 = extractor.extract(utf16, all);
        const auto wide32 = extractor.extract(utf32, all);
        ASSERT_EQ(wide16.size(), expected.size());
        ASSERT_EQ(wide32.size(), expected.size());
        for (auto i = size_t{0}; i < expected.size(); ++i) {
            EXPECT_EQ(encode<char>(decode(wide16[i].word)), expected[i].word);
            EXPECT_EQ(encode<char>(decode(wide32[i].word)), expected[i].word);
            EXPECT_EQ(wide16[i].weight, expected[i].weight);
            EXPECT_EQ(wide32[i].weight, expected[i].weight);
            ASSERT_EQ(wide16[i].positions.size(), expected[i].positions.size());
            for (auto j = size_t{0}; j < expected[i].positions.size(); ++j) {
                const auto &position = expected[i].positions[j];
                EXPECT_EQ(position.source.slice(utf8), expected[i].word);
                EXPECT_EQ(wide16[i].positions[j].runes, position.runes);
                EXPECT_EQ(wide32[i].positions[j].runes, position.runes);
                EXPECT_EQ(wide16[i].positions[j].source.slice(std::u16string_view{utf16}), wide16[i].word);
                EXPECT_EQ(wide32[i].positions[j].source.begin, position.runes.begin);
            }
        }
    };
    check_encoding(tfidf);
    check_encoding(textrank);
}

TEST(KeywordExtractorTest, WorkspaceReuseMatchesFreshCalls) {
    const auto tfidf = TfIdfExtractor{small_jieba(), IDF_FILE, STOP_WORDS_FILE};
    const auto textrank = TextRankExtractor{small_jieba(), STOP_WORDS_FILE};
    const auto inputs = std::vector<std::string_view>{
        "我是拖拉机学院手扶拖拉机专业的。不用多久，我就会升职加薪，当上CEO，走上人生巅峰。", "你好世界世界而且而且",
        "", "一部iPhone6"};

    auto workspace = KeywordWorkspace{};
    auto out = std::vector<Keyword<char>>{};
    for (const auto input : inputs) {
        tfidf.extract_into(input, all, out, workspace);
        EXPECT_EQ(format_keywords(out), format_keywords(tfidf.extract(input, all)));
        textrank.extract_into(input, 3, out, workspace);
        EXPECT_EQ(format_keywords(out), format_keywords(textrank.extract(input, 3)));
    }
    workspace.release();
    tfidf.extract_into(inputs[1], all, out, workspace);
    EXPECT_EQ(format_keywords(out), format_keywords(tfidf.extract(inputs[1], all)));
}

TEST(KeywordExtractorTest, RejectsInvalidConfiguration) {
    EXPECT_ANY_THROW((TextRankExtractor{small_jieba(), STOP_WORDS_FILE, {.damping = 1.5}}));
    EXPECT_ANY_THROW((TfIdfExtractor{small_jieba(), DICT_DIR "/missing_idf.utf8", STOP_WORDS_FILE}));
    // The main dictionary has three fields per line, so it is not a valid IDF table.
    EXPECT_ANY_THROW((TfIdfExtractor{small_jieba(), SMALL_DICT_FILE, STOP_WORDS_FILE}));
}

TEST(KeywordExtractorTest, ReportsLookupTags) {
    const auto tfidf = TfIdfExtractor{small_jieba(), IDF_FILE, STOP_WORDS_FILE};
    const auto textrank = TextRankExtractor{small_jieba(), STOP_WORDS_FILE};
    constexpr auto text = std::string_view{"我是拖拉机学院手扶拖拉机专业的。不用多久，我就会升职加薪，当上CEO，走上人生巅峰。"};
    for (const auto &keywords : {tfidf.extract(text, all), textrank.extract(text, all)}) {
        ASSERT_FALSE(keywords.empty());
        for (const auto &keyword : keywords) {
            EXPECT_EQ(keyword.tag, small_jieba().lookup_tag(keyword.word)) << keyword.word;
        }
    }
    const auto english = tfidf.extract(std::string_view{"一部iPhone6"}, 1);
    ASSERT_EQ(english.size(), 1U);
    EXPECT_EQ(english[0].tag, pos::eng);
}

// TF-IDF scores each word independently, so filtering by tag only removes words from the unfiltered result.
TEST(KeywordExtractorTest, TfIdfAllowedTagsFilterCandidates) {
    const auto tfidf = TfIdfExtractor{small_jieba(), IDF_FILE, STOP_WORDS_FILE};
    const auto allowed = std::vector{pos::n, pos::v, pos::vn};
    const auto filtered = TfIdfExtractor{small_jieba(), IDF_FILE, STOP_WORDS_FILE, {.allowed_tags = allowed}};
    constexpr auto text = std::string_view{"我是拖拉机学院手扶拖拉机专业的。不用多久，我就会升职加薪，当上CEO，走上人生巅峰。"};

    auto expected = tfidf.extract(text, all);
    std::erase_if(expected, [&](const Keyword<char> &keyword) { return std::ranges::find(allowed, keyword.tag) == allowed.end(); });
    ASSERT_FALSE(expected.empty());
    EXPECT_EQ(format_keywords(filtered.extract(text, all)), format_keywords(expected));
    EXPECT_LT(expected.size(), tfidf.extract(text, all).size());

    const auto none = TfIdfExtractor{small_jieba(), IDF_FILE, STOP_WORDS_FILE, {.allowed_tags = {PosTag{"zz"}}}};
    EXPECT_TRUE(none.extract(text, all).empty());
}

// Removed words leave the TextRank graph entirely, so the remaining words are ranked among themselves.
TEST(KeywordExtractorTest, TextRankAllowedTagsFilterCandidates) {
    const auto textrank = TextRankExtractor{small_jieba(), STOP_WORDS_FILE};
    const auto python = TextRankExtractor{
        small_jieba(), STOP_WORDS_FILE,
        {.allowed_tags = std::vector<PosTag>(python_textrank_tags.begin(), python_textrank_tags.end())}};
    constexpr auto text = std::string_view{"我是拖拉机学院手扶拖拉机专业的。不用多久，我就会升职加薪，当上CEO，走上人生巅峰。"};

    const auto keywords = python.extract(text, all);
    ASSERT_FALSE(keywords.empty());
    EXPECT_EQ(keywords[0].weight, 1.0);
    for (const auto &keyword : keywords) {
        EXPECT_NE(std::ranges::find(python_textrank_tags, keyword.tag), python_textrank_tags.end()) << keyword.word;
    }
    for (const auto &keyword : textrank.extract(text, all)) {
        const auto allowed = std::ranges::find(python_textrank_tags, keyword.tag) != python_textrank_tags.end();
        const auto kept = std::ranges::any_of(keywords, [&](const Keyword<char> &k) { return k.word == keyword.word; });
        EXPECT_EQ(kept, allowed) << keyword.word;
    }
}
