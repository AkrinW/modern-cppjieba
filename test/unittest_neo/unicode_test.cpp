#include "gtest/gtest.h"
#include "neo/Traits.hpp"
#include "neo/Unicode.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

using namespace neo_cppjieba;

// ─── decode_with_source: UTF-8 ──────────────────────────────────────────────

TEST(UnicodeWithSourceTest, Utf8Ascii) {
    auto input = std::string{"Hello"};
    auto result = decode_with_offset(input);

    ASSERT_EQ(result.runes.size(), 5u);
    ASSERT_EQ(result.offsets.size(), 6u); // runes.size() + 1
    EXPECT_EQ(result.runes[0], U'H');
    EXPECT_EQ(result.runes[4], U'o');

    // All ASCII: each rune is 1 byte
    for (size_t i = 0; i <= 5; ++i) {
        EXPECT_EQ(result.offsets[i], i);
    }
}

TEST(UnicodeWithSourceTest, Utf8Chinese) {
    auto input = std::string{"你好世界"};
    auto result = decode_with_offset(input);

    ASSERT_EQ(result.runes.size(), 4u);
    ASSERT_EQ(result.offsets.size(), 5u);

    EXPECT_EQ(result.runes[0], U'你');
    EXPECT_EQ(result.runes[1], U'好');
    EXPECT_EQ(result.runes[2], U'世');
    EXPECT_EQ(result.runes[3], U'界');

    // Each Chinese character is 3 bytes in UTF-8
    EXPECT_EQ(result.offsets[0], 0u);
    EXPECT_EQ(result.offsets[1], 3u);
    EXPECT_EQ(result.offsets[2], 6u);
    EXPECT_EQ(result.offsets[3], 9u);
    EXPECT_EQ(result.offsets[4], 12u);
}

TEST(UnicodeWithSourceTest, Utf8Mixed) {
    // Mix of ASCII and multi-byte characters
    auto input = std::string{"a你b好c"};
    auto result = decode_with_offset(input);

    ASSERT_EQ(result.runes.size(), 5u);
    EXPECT_EQ(result.runes[0], U'a');
    EXPECT_EQ(result.runes[1], U'你');
    EXPECT_EQ(result.runes[2], U'b');
    EXPECT_EQ(result.runes[3], U'好');
    EXPECT_EQ(result.runes[4], U'c');

    // offsets: a(1) 你(3) b(1) 好(3) c(1) = total 9 bytes
    EXPECT_EQ(result.offsets[0], 0u);
    EXPECT_EQ(result.offsets[1], 1u);
    EXPECT_EQ(result.offsets[2], 4u);
    EXPECT_EQ(result.offsets[3], 5u);
    EXPECT_EQ(result.offsets[4], 8u);
    EXPECT_EQ(result.offsets[5], 9u);
}

TEST(UnicodeWithSourceTest, Utf8FourByte) {
    // Emoji: 😀 = U+1F600 = 4 bytes in UTF-8
    auto input = std::string{"😀"};
    auto result = decode_with_offset(input);

    ASSERT_EQ(result.runes.size(), 1u);
    EXPECT_EQ(result.runes[0], U'\U0001F600');
    EXPECT_EQ(result.offsets[0], 0u);
    EXPECT_EQ(result.offsets[1], 4u);
}

TEST(UnicodeWithSourceTest, Utf8Empty) {
    auto input = std::string{};
    auto result = decode_with_offset(input);

    EXPECT_TRUE(result.runes.empty());
    ASSERT_EQ(result.offsets.size(), 1u);
    EXPECT_EQ(result.offsets[0], 0u);
}

// ─── encode_range ───────────────────────────────────────────────────────────

TEST(UnicodeWithSourceTest, EncodeRangeFull) {
    auto input = std::string{"你好世界"};
    auto result = decode_with_offset(input);

    auto view = as_view(input);

    const auto &runes = result.get_runes();
    const auto &offsets = result.get_offsets();

    auto range = WordRange{0, static_cast<uint32_t>(runes.size())};
    auto encoded = encode(view, offsets, range);

    EXPECT_EQ(encoded, input);
}

TEST(UnicodeWithSourceTest, EncodeRangeSubstring) {
    auto input = std::string{"你好世界"};
    auto result = decode_with_offset(input);

    auto view = as_view(input);

    const auto &offsets = result.get_offsets();

    // Encode just "好世"
    auto range = WordRange{1, 3};
    auto encoded = encode(view, offsets, range);
    EXPECT_EQ(encoded, "好世");
}

TEST(UnicodeWithSourceTest, EncodeRangeEmpty) {
    auto input = std::string{"你好"};
    auto result = decode_with_offset(input);

    auto view = as_view(input);
    const auto &offsets = result.get_offsets();

    auto range = WordRange{0, 0};
    auto encoded = encode(view, offsets, range);
    EXPECT_TRUE(encoded.empty());
}

TEST(UnicodeWithSourceTest, EncodeAll) {
    auto input = std::string{"Hello 你好"};
    auto result = decode_with_offset(input);

    auto view = as_view(input);
    const auto &offsets = result.get_offsets();

    auto range = WordRange{0, static_cast<uint32_t>(result.get_runes().size())};
    auto encoded = encode(view, offsets, range);
    EXPECT_EQ(encoded, input);
}

// ─── source_of ──────────────────────────────────────────────────────────────

TEST(UnicodeWithSourceTest, SourceOf) {
    auto input = std::string{"a你b"};
    auto result = decode_with_offset(input);

    auto view = as_view(input);
    const auto &offsets = result.get_offsets();

    EXPECT_EQ(encode(view, offsets, WordRange{0, 1}), "a");
    EXPECT_EQ(encode(view, offsets, WordRange{1, 2}), "你");
    EXPECT_EQ(encode(view, offsets, WordRange{2, 3}), "b");
}

// ─── Roundtrip: decode_with_source + encode_range == normal decode + encode ─

TEST(UnicodeWithSourceTest, RoundtripMatchesNormalEncode) {
    auto input = std::string{"南京市长江大桥欢迎您！Hello, world! 😀🌍"};
    auto sourced = decode_with_offset(input);
    auto normal = decode(input);

    // Runes must match
    ASSERT_EQ(sourced.runes.size(), normal.size());
    for (size_t i = 0; i < normal.size(); ++i) {
        EXPECT_EQ(sourced.runes[i], normal[i]);
    }

    // Full encode must match
    auto view = as_view(input);
    const auto &offsets = sourced.get_offsets();
    auto fast_encoded = encode(view, offsets, WordRange{0, static_cast<uint32_t>(sourced.get_runes().size())});
    auto normal_encoded = encode(std::span<const Rune>(normal));
    EXPECT_EQ(fast_encoded, normal_encoded);
    EXPECT_EQ(fast_encoded, input);

    // Sub-range encode must match for various ranges
    for (size_t start = 0; start < normal.size(); ++start) {
        for (size_t count = 0; count + start <= normal.size() && count <= 5; ++count) {
            auto fast =
                encode(view, offsets, WordRange{static_cast<uint32_t>(start), static_cast<uint32_t>(start + count)});
            auto slow = encode(std::span<const Rune>(normal.data() + start, count));
            EXPECT_EQ(fast, slow) << "Mismatch at start=" << start << " count=" << count;
        }
    }
}

// ─── UTF-16 ─────────────────────────────────────────────────────────────────

TEST(UnicodeWithSourceTest, Utf16Basic) {
    auto input = std::u16string{u"你好世界"};
    auto result = decode_with_offset(input);

    auto view = as_view(input);
    const auto &offsets = result.get_offsets();

    ASSERT_EQ(result.runes.size(), 4u);
    EXPECT_EQ(result.runes[0], U'你');
    EXPECT_EQ(result.runes[3], U'界');

    // Each CJK character is 1 code unit in UTF-16
    for (size_t i = 0; i <= 4; ++i) {
        EXPECT_EQ(result.offsets[i], i);
    }

    auto encoded = encode(view, offsets, WordRange{0, static_cast<uint32_t>(result.get_runes().size())});
    EXPECT_EQ(encoded, input);
    EXPECT_EQ(encode(view, offsets, WordRange{1, 3}), u"好世");
}

TEST(UnicodeWithSourceTest, Utf16Surrogate) {
    // 😀 = U+1F600, requires surrogate pair in UTF-16
    auto input = std::u16string{u"😀"};
    auto result = decode_with_offset(input);

    auto view = as_view(input);
    const auto &offsets = result.get_offsets();

    ASSERT_EQ(result.runes.size(), 1u);
    EXPECT_EQ(result.runes[0], U'\U0001F600');
    EXPECT_EQ(result.offsets[0], 0u);
    EXPECT_EQ(result.offsets[1], 2u); // 2 code units for surrogate pair

    auto encoded = encode(view, offsets, WordRange{0, static_cast<uint32_t>(result.get_runes().size())});
    EXPECT_EQ(encoded, input);
}

// ─── UTF-32 ─────────────────────────────────────────────────────────────────

TEST(UnicodeWithSourceTest, Utf32Basic) {
    auto input = std::u32string{U"你好世界"};
    auto result = decode_with_offset(input);

    auto view = as_view(input);
    const auto &offsets = result.get_offsets();

    ASSERT_EQ(result.runes.size(), 4u);
    EXPECT_EQ(result.runes[0], U'你');
    EXPECT_EQ(result.runes[3], U'界');

    // Each character is 1 code unit in UTF-32
    for (size_t i = 0; i <= 4; ++i) {
        EXPECT_EQ(result.offsets[i], i);
    }

    auto encoded = encode(view, offsets, WordRange{0, static_cast<uint32_t>(result.get_runes().size())});
    EXPECT_EQ(encoded, input);
    EXPECT_EQ(encode(view, offsets, WordRange{1, 3}), U"好世");
}

// ─── Consistency: decode_with_source runes == decode runes ──────────────────

TEST(UnicodeWithSourceTest, ConsistencyWithDecode) {
    auto inputs = std::vector<std::string>{
        "", "ASCII only", "你好世界", "Mixed 混合 text 文本 123", "😀🌍🎉", "emoji 😀 in 中间 of text",
    };

    for (const auto &input : inputs) {
        auto normal = decode(input);
        auto sourced = decode_with_offset(input);

        ASSERT_EQ(sourced.runes.size(), normal.size()) << "Input: " << input;
        for (size_t i = 0; i < normal.size(); ++i) {
            EXPECT_EQ(sourced.runes[i], normal[i]) << "Input: " << input << " index: " << i;
        }
    }
}
