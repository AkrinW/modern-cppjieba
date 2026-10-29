#include "gtest/gtest.h"
#include "neo/Jieba.hpp"
#include "neo/Utf.hpp"
#include "neo/algorithm/PosTagging.hpp"

#include "test_paths.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace neo_cppjieba;

namespace {

inline constexpr auto DICT_FILE = std::string_view{DICT_DIR "/jieba.dict.utf8"};
inline constexpr auto HMM_MODEL_FILE = std::string_view{DICT_DIR "/hmm_model.utf8"};
inline constexpr auto USER_DICT_FILE = std::string_view{TEST_DATA_DIR "/userdict.utf8"};

constexpr auto all_modes = std::array{CutMode::MIX,  CutMode::MIX_NO_HMM, CutMode::MP,           CutMode::HMM,
                                      CutMode::FULL, CutMode::SEARCH,     CutMode::SEARCH_NO_HMM};

constexpr auto sentence = std::string_view{"我是蓝翔技工拖拉机学院手扶拖拉机专业的。不用多久，我就会升职加薪，当上总经"
                                           "理，出任CEO，迎娶白富美，走上人生巅峰。"};

auto test_jieba() -> const Jieba & {
    static const auto jieba = Jieba{DICT_FILE, HMM_MODEL_FILE, ""};
    return jieba;
}

auto user_jieba() -> const Jieba & {
    static const auto jieba = Jieba{DICT_FILE, HMM_MODEL_FILE, USER_DICT_FILE};
    return jieba;
}

// Format pairs as the legacy CppJieba tests print them.
auto format_pairs(const std::vector<TaggedWord<char>> &pairs) -> std::string {
    auto out = std::string{"["};
    for (const auto &[word, tag] : pairs) {
        out += std::format("{}{}:{}", out.size() > 1 ? ", " : "", word, tag.to_string_view());
    }
    return out + "]";
}

auto untagged(std::u32string_view word) -> PosTag {
    return detail::untagged_word_tag(std::span<const Rune>{word.data(), word.size()});
}

} // namespace

TEST(TaggingTest, UntaggedWordRuleMatchesCppJieba) {
    EXPECT_EQ(untagged(U""), pos::x);
    EXPECT_EQ(untagged(U"蓝翔"), pos::x);
    EXPECT_EQ(untagged(U"CEO"), pos::eng);
    EXPECT_EQ(untagged(U"iPhone6"), pos::eng);
    EXPECT_EQ(untagged(U"2024"), pos::m);
    EXPECT_EQ(untagged(U"中文abc"), pos::eng);
    // CppJieba stops once ASCII runes reach half the word length.
    EXPECT_EQ(untagged(U"7"), pos::x);
    EXPECT_EQ(untagged(U"a"), pos::x);
    EXPECT_EQ(untagged(U"12ab"), pos::m);
    EXPECT_EQ(untagged(U"ab12"), pos::eng);
    // Unlike Python jieba's posseg, a decimal point is not part of a number.
    EXPECT_EQ(untagged(U"3.14"), pos::eng);
}

// Expected strings come from CppJieba's pos_tagger_test.cpp.
TEST(TaggingTest, MixMatchesCppJiebaTag) {
    EXPECT_EQ(format_pairs(test_jieba().cut_strings<Tagging::Pos>(sentence, CutMode::MIX)),
              "[我:r, 是:v, 蓝翔:x, 技工:n, 拖拉机:n, 学院:n, 手扶拖拉机:n, 专业:n, 的:uj, 。:x, 不用:v, 多久:m, ，:x, "
              "我:r, 就:d, 会:v, 升职:v, 加薪:nr, ，:x, 当上:t, 总经理:n, ，:x, 出任:v, CEO:eng, ，:x, 迎娶:v, 白富:x, "
              "美:ns, ，:x, 走上:v, 人生:n, 巅峰:n, 。:x]");
}

TEST(TaggingTest, UserDictionaryTagsMatchCppJiebaTag) {
    EXPECT_EQ(
        format_pairs(user_jieba().cut_strings<Tagging::Pos>(sentence, CutMode::MIX)),
        "[我:r, 是:v, 蓝翔:nz, 技工:n, 拖拉机:n, 学院:n, 手扶拖拉机:n, 专业:n, 的:uj, 。:x, 不用:v, 多久:m, ，:x, "
        "我:r, 就:d, 会:v, 升职:v, 加薪:nr, ，:x, 当上:t, 总经理:n, ，:x, 出任:v, CEO:eng, ，:x, 迎娶:v, 白富:x, "
        "美:ns, ，:x, 走上:v, 人生:n, 巅峰:n, 。:x]");
    EXPECT_EQ(format_pairs(user_jieba().cut_strings<Tagging::Pos>("iPhone6手机的最大特点是很容易弯曲。", CutMode::MIX)),
              "[iPhone6:eng, 手机:n, 的:uj, 最大:a, 特点:n, 是:v, 很:zg, 容易:a, 弯曲:v, 。:x]");
}

TEST(TaggingTest, LookupTagUsesDictionaryThenRule) {
    const auto &jieba = user_jieba();
    EXPECT_EQ(jieba.lookup_tag("拖拉机"), pos::n);
    EXPECT_EQ(jieba.lookup_tag("蓝翔"), PosTag{"nz"});
    EXPECT_EQ(jieba.lookup_tag(u"拖拉机"), pos::n);
    EXPECT_EQ(jieba.lookup_tag(U"拖拉机"), pos::n);
    // A user word without a tag falls back to the rule.
    EXPECT_EQ(jieba.lookup_tag("iPhone6"), pos::eng);
    EXPECT_EQ(jieba.lookup_tag("白富"), pos::x);
    EXPECT_EQ(jieba.lookup_tag(""), pos::x);

    const auto runes = Utf::decode(std::string_view{"总经理"});
    EXPECT_EQ(jieba.lookup_tag(std::span<const Rune>{runes}), pos::n);
}

// Tagging adds tags to the untagged segmentation without changing positions, in every mode.
TEST(TaggingTest, TaggedPositionsMatchUntaggedCut) {
    const auto &jieba = user_jieba();
    for (const auto mode : all_modes) {
        SCOPED_TRACE(static_cast<int>(mode));
        const auto tokens = jieba.cut(sentence, mode);
        const auto tagged = jieba.cut<Tagging::Pos>(sentence, mode);
        ASSERT_EQ(tagged.size(), tokens.size());
        EXPECT_EQ(tagged.source().data(), sentence.data());
        for (auto i = size_t{0}; i < tokens.size(); ++i) {
            EXPECT_EQ(tagged[i].word, tokens[i].word);
            EXPECT_EQ(tagged[i].position, tokens[i].position);
            EXPECT_EQ(tagged[i].tag, jieba.lookup_tag(tokens[i].word));
            EXPECT_EQ(tagged.positions()[i].position, tokens.positions()[i]);
        }
    }
}

TEST(TaggingTest, AllTaggedOutputsAgree) {
    const auto &jieba = user_jieba();
    auto workspace = Workspace{};
    auto positions = std::vector<TaggedPosition>{};
    auto words = std::vector<TaggedWord<char>>{};
    for (const auto mode : all_modes) {
        SCOPED_TRACE(static_cast<int>(mode));
        const auto expected = jieba.cut_strings<Tagging::Pos>(sentence, mode);

        const auto tagged = jieba.cut_with_workspace<Tagging::Pos>(sentence, mode, workspace);
        auto from_tokens = std::vector<TaggedWord<char>>{};
        for (const auto token : tagged) {
            from_tokens.push_back({std::string{token.word}, token.tag});
        }
        EXPECT_EQ(from_tokens, expected);

        jieba.cut_into(sentence, mode, words, workspace);
        EXPECT_EQ(words, expected);

        jieba.cut_into(sentence, mode, positions, workspace);
        ASSERT_EQ(positions.size(), expected.size());
        for (auto i = size_t{0}; i < positions.size(); ++i) {
            EXPECT_EQ(positions[i].position.source.slice(sentence), expected[i].word);
            EXPECT_EQ(positions[i].tag, expected[i].tag);
        }

        auto visited = std::vector<TaggedWord<char>>{};
        jieba.cut_each<Tagging::Pos>(
            sentence, mode,
            [&](TaggedTokenView<char> token) { visited.push_back({std::string{token.word}, token.tag}); }, workspace);
        EXPECT_EQ(visited, expected);

        const auto owned = jieba.cut_owned<Tagging::Pos>(std::string{sentence}, mode);
        auto from_owned = std::vector<TaggedWord<char>>{};
        for (const auto token : owned) {
            from_owned.push_back({std::string{token.word}, token.tag});
        }
        EXPECT_EQ(from_owned, expected);
    }
}

TEST(TaggingTest, TagsDoNotDependOnEncoding) {
    const auto &jieba = user_jieba();
    const auto utf8 = jieba.cut_strings<Tagging::Pos>(sentence, CutMode::MIX);
    const auto utf16 = jieba.cut_strings<Tagging::Pos>(Utf::encode<char16_t>(Utf::decode(sentence)), CutMode::MIX);
    const auto utf32 = jieba.cut<Tagging::Pos>(std::u32string_view{U"出任CEO，迎娶白富美"}, CutMode::MIX);
    ASSERT_EQ(utf16.size(), utf8.size());
    for (auto i = size_t{0}; i < utf8.size(); ++i) {
        EXPECT_EQ(utf16[i].word, Utf::encode<char16_t>(Utf::decode(utf8[i].word)));
        EXPECT_EQ(utf16[i].tag, utf8[i].tag);
    }
    auto tags = std::vector<std::string>{};
    for (const auto token : utf32) {
        tags.push_back(token.tag.to_string());
    }
    EXPECT_EQ(tags, (std::vector<std::string>{"v", "eng", "x", "v", "x", "ns"}));
}

TEST(TaggingTest, EmptyInputAndOutputReuse) {
    const auto &jieba = test_jieba();
    auto workspace = Workspace{};
    auto words = std::vector<TaggedWord<char>>{{"stale", pos::n}};
    jieba.cut_into(std::string_view{}, CutMode::MIX, words, workspace);
    EXPECT_TRUE(words.empty());
    EXPECT_TRUE(jieba.cut<Tagging::Pos>(std::string_view{}, CutMode::MIX).empty());
}

// Tagged borrowing follows the untagged contract: owning temporaries are rejected.
template <typename Input>
concept TaggedCutInvocable =
    requires(const Jieba &jieba) { jieba.cut<Tagging::Pos>(std::declval<Input>(), CutMode::MIX); };

static_assert(TaggedCutInvocable<std::string_view>);
static_assert(TaggedCutInvocable<const std::string &>);
static_assert(!TaggedCutInvocable<std::string>);
static_assert(std::same_as<TaggedTokens<char>::value_type, TaggedTokenView<char>>);
static_assert(std::same_as<Tokens<char>::value_type, TokenView<char>>);
static_assert(std::same_as<Tokens<char, Tagging::Pos>, TaggedTokens<char>>);
static_assert(
    std::same_as<decltype(std::declval<const Jieba &>().cut_strings("x", CutMode::MIX)), std::vector<std::string>>);
static_assert(std::same_as<decltype(std::declval<const Jieba &>().cut_strings<Tagging::Pos>("x", CutMode::MIX)),
                           std::vector<TaggedWord<char>>>);
static_assert(sizeof(TaggedPosition) == sizeof(TokenPosition) + sizeof(PosTag)
              || alignof(TokenPosition) > alignof(PosTag));
