#include "../TestUtils.hpp"
#include "gtest/gtest.h"
#include "neo/Config.hpp"
#include "neo/Jieba.hpp"
#include "neo/Unicode.hpp"

#include "test_paths.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace neo_cppjieba;

inline constexpr auto DICT_FILE = std::string_view{DICT_DIR "/jieba.dict.utf8"};
inline constexpr auto HMM_MODEL_FILE = std::string_view{DICT_DIR "/hmm_model.utf8"};

namespace {

// An adapter can return different views; decoding and output copying must use the same conversion.
struct ChangingTextView {
    using value_type = char;
    mutable std::size_t conversions = 0;

    operator std::string_view() const {
        return ++conversions == 1 ? "中国" : "x";
    }
};

// Source-copy optimizations must preserve code-unit offsets for every supported character encoding.
template <typename CharT>
class JiebaCharacterTest : public ::testing::Test {};

using InputCharacterTypes = ::testing::Types<char, char8_t, char16_t, char32_t, wchar_t>;
TYPED_TEST_SUITE(JiebaCharacterTest, InputCharacterTypes);

} // namespace

TYPED_TEST(JiebaCharacterTest, DirectCutMatchesCheckedOutputForEveryMethod) {
    const auto jieba = Jieba{DICT_FILE, HMM_MODEL_FILE};
    const auto inputs = std::array{Unicode{}, Unicode{U'中', U'国', U'科', U'学', U'院'},
                                   Unicode{U'甲', U' ', U'A', U'\0', U'中', U'国', U'😀', U'𠀀', U'乙', U'!'}};
    for (const auto &runes : inputs) {
        const auto text = neo_cppjieba::encode<TypeParam>(runes);
        const auto source = as_view(text);
        const auto decoded = Jieba::decode_with_offset(source);
        const auto compare = [&]<CutMethod Method, bool Hmm>() {
            const auto ranges = jieba.template cut<Method, Hmm>(decoded);
            const auto expected = Jieba::encode_words(source, decoded.offsets, ranges);
            EXPECT_EQ((jieba.template cut<Method, Hmm>(text)), expected);
        };
        compare.template operator()<CutMethod::MIX, true>();
        compare.template operator()<CutMethod::MIX, false>();
        compare.template operator()<CutMethod::MP, true>();
        compare.template operator()<CutMethod::HMM, true>();
        compare.template operator()<CutMethod::FULL, true>();
        compare.template operator()<CutMethod::SEARCH, true>();
        compare.template operator()<CutMethod::SEARCH, false>();
    }
}

TEST(JiebaNeoTest, DirectCutUsesTheSameViewForDecodingAndCopying) {
    const auto jieba = Jieba{DICT_FILE, HMM_MODEL_FILE};
    const auto input = ChangingTextView{};
    EXPECT_EQ(jieba.cut(input), (std::vector<std::string>{"中国"}));
    EXPECT_EQ(input.conversions, 1);
}

TEST(JiebaNeoTest, PublicSourceEncodingStillRejectsInvalidOffsets) {
    const auto source = std::string_view{"abc"};
    const auto offsets = std::array<std::uint32_t, 2>{0, 4};
    const auto ranges = std::array{WordRange{0, 1}};
    EXPECT_THROW(Jieba::encode(source, offsets, ranges.front()), LogConfig::Exception);
    EXPECT_THROW(Jieba::encode_words(source, offsets, ranges), LogConfig::Exception);
}

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

TEST(JiebaNeoTest, ByteInputsReturnUtf8Words) {
    const auto jieba = Jieba{DICT_FILE, HMM_MODEL_FILE};
    const auto input = std::u8string_view{u8"他来到了网易杭研大厦"};
    const auto bytes = std::as_bytes(std::span{input.data(), input.size()});
    const auto unsigned_bytes = std::vector<unsigned char>(input.begin(), input.end());
    const auto signed_bytes = std::vector<signed char>(input.begin(), input.end());
    const auto expected = std::vector<std::string>{"他", "来到", "了", "网易", "杭研", "大厦"};

    EXPECT_EQ(jieba.cut(bytes), expected);
    EXPECT_EQ(jieba.cut(unsigned_bytes), expected);
    EXPECT_EQ(jieba.cut(signed_bytes), expected);
}

TEST(JiebaNeoTest, ByteOffsetPipelinePreservesOriginalUtf8Words) {
    const auto jieba = Jieba{DICT_FILE, HMM_MODEL_FILE};
    const auto input = std::u8string_view{u8"中国科学院"};
    const auto bytes = std::as_bytes(std::span{input.data(), input.size()});
    const auto decoded = Jieba::decode_with_offset(bytes);
    const auto ranges = jieba.cut<CutMethod::SEARCH>(decoded);
    const auto words = Jieba::encode_words(as_view(bytes), decoded.offsets, ranges);

    EXPECT_EQ(words, (std::vector<std::string>{"中国", "科学", "学院", "科学院", "中国科学院"}));
}

TEST(JiebaNeoTest, EmptyByteInputProducesNoWords) {
    const auto jieba = Jieba{DICT_FILE, HMM_MODEL_FILE};
    EXPECT_TRUE(jieba.cut(std::span<const std::byte>{}).empty());
}

TEST(JiebaNeoTest, InvalidByteInputThrowsTheConfiguredException) {
    const auto jieba = Jieba{DICT_FILE, HMM_MODEL_FILE};
    const auto input = std::array{std::byte{0xE4}, std::byte{0xB8}};
    EXPECT_THROW(jieba.cut(input), LogConfig::Exception);
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
