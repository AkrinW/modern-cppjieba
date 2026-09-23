#include "gtest/gtest.h"
#include "neo/Config.hpp"
#include "neo/Traits.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace neo_cppjieba;

namespace {

// A custom view conversion may only be used through the const input contract.
struct MutableViewSource {
    using value_type = char;
    operator std::string_view() {
        return "mutable";
    }
};

// Input adapters may report their own exceptions without terminating the process.
struct ThrowingViewSource {
    using value_type = char;
    operator std::string_view() const {
        throw std::logic_error{"view conversion failed"};
    }
};

// Character extraction must use the same const range access as as_view.
struct ConstOnlyRange {
    std::array<char, 2> characters{'a', 'b'};
    auto begin() const -> const char * {
        return characters.data();
    }
    auto end() const -> const char * {
        return characters.data() + characters.size();
    }
    auto begin() -> char * = delete;
    auto end() -> char * = delete;
};

// Test whether a value category may expose a borrowed view.
template <typename T>
concept CanView = requires(T &&input) { as_view(std::forward<T>(input)); };

} // namespace

TEST(TraitsTest, OutputCharactersMustBeUnqualifiedCharacterTypes) {
    static_assert(CharType<char> && CharType<char8_t> && CharType<char16_t> && CharType<char32_t> && CharType<wchar_t>);
    static_assert(!CharType<const char> && !CharType<volatile char> && !CharType<char &>);
    static_assert(!CharType<unsigned char> && !CharType<std::byte> && !CharType<int>);
}

TEST(TraitsTest, EncodingLookupNormalizesCvAndReferenceQualifiers) {
    static_assert(encoding_of_v<const char &> == Encoding::UTF8);
    static_assert(encoding_of_v<char8_t> == Encoding::UTF8);
    static_assert(encoding_of_v<const char16_t> == Encoding::UTF16);
    static_assert(encoding_of_v<char32_t &&> == Encoding::UTF32);
    static_assert(encoding_of_v<wchar_t> == (sizeof(wchar_t) == 2 ? Encoding::UTF16 : Encoding::UTF32));
}

TEST(TraitsTest, StringLikeRequiresReadableConstCharacterStorage) {
    static_assert(StringLike<std::string> && StringLike<std::u8string> && StringLike<std::u16string>);
    static_assert(StringLike<std::u32string> && StringLike<std::wstring> && StringLike<std::string_view>);
    static_assert(StringLike<std::span<const char>> && StringLike<std::vector<char>>);
    static_assert(StringLike<std::array<char, 3>> && StringLike<const char *> && StringLike<char[3]>);
    static_assert(StringLike<ThrowingViewSource> && StringLike<ConstOnlyRange>);
    static_assert(!StringLike<MutableViewSource> && !StringLike<std::span<volatile char>>);
    static_assert(!StringLike<std::vector<int>> && !StringLike<int> && !StringLike<void>);
    static_assert(!StringLike<char[]> && !StringLike<std::nullptr_t>);
}

TEST(TraitsTest, BorrowedViewsRejectOwningTemporaries) {
    static_assert(CanView<std::string &> && CanView<const std::string &>);
    static_assert(CanView<std::vector<char> &> && CanView<std::array<char, 3> &>);
    static_assert(CanView<std::string_view> && CanView<std::span<const char>> && CanView<const char *>);
    static_assert(!CanView<std::string> && !CanView<const std::string> && !CanView<std::vector<char>>);
    static_assert(!CanView<std::array<char, 3>>);
}

TEST(TraitsTest, FixedArrayDoesNotRequireTerminatingNul) {
    const char input[] = {'a', 'b', 'c'};
    EXPECT_EQ(as_view(input), "abc");
}

TEST(TraitsTest, LiteralPreservesEmbeddedNulAndOmitsTerminator) {
    constexpr auto view = as_view("a\0b");
    static_assert(view.size() == 3 && view[1] == '\0');
    EXPECT_EQ(view, (std::string_view{"a\0b", 3}));
}

TEST(TraitsTest, SizedRangeRetainsTrailingNul) {
    const auto input = std::array{'a', '\0', 'b', '\0'};
    const auto view = as_view(input);
    EXPECT_EQ(view.size(), input.size());
    EXPECT_EQ(view.data(), input.data());
    EXPECT_EQ(view.back(), '\0');
}

TEST(TraitsTest, EmptyRangesProduceEmptyViews) {
    const auto input = std::vector<char>{};
    EXPECT_TRUE(as_view(input).empty());
    EXPECT_TRUE(as_view(std::span<const char>{}).empty());
    EXPECT_TRUE(as_view("").empty());
}

TEST(TraitsTest, ConstOnlyRangeUsesConstAccess) {
    auto input = ConstOnlyRange{};
    EXPECT_EQ(as_view(input), "ab");
}

TEST(TraitsTest, CStringPointerStopsAtFirstNul) {
    const char *input = "a\0b";
    EXPECT_EQ(as_view(input), "a");
}

TEST(TraitsTest, NullCStringThrowsConfiguredException) {
    const char *input = nullptr;
    EXPECT_THROW(as_view(input), LogConfig::Exception);
}

TEST(TraitsTest, CustomConversionExceptionPropagates) {
    const auto input = ThrowingViewSource{};
    EXPECT_THROW(as_view(input), std::logic_error);
}
