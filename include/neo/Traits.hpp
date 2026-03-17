#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>
#include <type_traits>
namespace neo_cppjieba {
// Encoding represents the encoding type of a string.
enum class Encoding : uint8_t { UTF8, UTF16, UTF32 };

// encoding_of is a type trait that maps a character type to its corresponding encoding.
template <typename T>
struct encoding_of;

template <>
struct encoding_of<char> {
    static constexpr auto value = Encoding::UTF8;
};

template <>
struct encoding_of<char8_t> {
    static constexpr auto value = Encoding::UTF8;
};

template <>
struct encoding_of<char16_t> {
    static constexpr auto value = Encoding::UTF16;
};

template <>
struct encoding_of<char32_t> {
    static constexpr auto value = Encoding::UTF32;
};

template <>
struct encoding_of<wchar_t> {
    static constexpr auto value = sizeof(wchar_t) == 2 ? Encoding::UTF16 : Encoding::UTF32;
};

// encoding_of_v is a helper variable template that provides a convenient way to access the encoding type for a given
// character type.
template <typename T>
constexpr auto encoding_of_v = encoding_of<T>::value;

// ── Concepts & as_view ─────────────────────────────────────────────────
/// CharType: a character type whose encoding is known.
template <typename T>
concept CharType = std::same_as<std::remove_cv_t<T>, char> || std::same_as<std::remove_cv_t<T>, char8_t>
                   || std::same_as<std::remove_cv_t<T>, char16_t> || std::same_as<std::remove_cv_t<T>, char32_t>
                   || std::same_as<std::remove_cv_t<T>, wchar_t>;

// Use a consteval function and if constexpr to replace complex SFINAE template specializations,
// avoiding ambiguity during type extraction.
template <typename T>
consteval auto get_char_type() {
    using Decayed = std::decay_t<T>;
    if constexpr (std::is_pointer_v<Decayed>) {
        // Pointer or decayed C-array (e.g., const char*, char[N])
        return std::type_identity<std::remove_cv_t<std::remove_pointer_t<Decayed>>>{};
    } else if constexpr (std::ranges::contiguous_range<T>) {
        // C++20 contiguous memory range (e.g., std::string, std::vector, std::span, std::array)
        return std::type_identity<std::remove_cv_t<std::ranges::range_value_t<T>>>{};
    } else if constexpr (requires { typename Decayed::value_type; }) {
        // Other non-standard custom containers with a nested value_type
        return std::type_identity<std::remove_cv_t<typename Decayed::value_type>>{};
    } else {
        return std::type_identity<void>{};
    }
}

// Extract the underlying character type
template <typename T>
using resolve_char_type_t = typename decltype(get_char_type<T>())::type;

// Core StringLike Concept: any type convertible to a basic_string_view via as_view.
//  - Must have a valid CharType.
//  - Must be implicitly convertible to string_view, OR be a contiguous range with a known size.
template <typename T>
concept StringLike = CharType<resolve_char_type_t<T>>
                     && (std::convertible_to<T, std::basic_string_view<resolve_char_type_t<T>>>
                         || (std::ranges::contiguous_range<T> && std::ranges::sized_range<T>));

// Unified as_view implementation
template <StringLike T>
constexpr auto as_view(const T &input) noexcept -> std::basic_string_view<resolve_char_type_t<T>> {
    using CharT = resolve_char_type_t<T>;

    if constexpr (std::convertible_to<T, std::basic_string_view<CharT>>) {
        // Branch A: std::string, std::string_view, const char*, string literals, etc.
        return std::basic_string_view<CharT>{input};
    } else {
        // Branch B: std::vector<CharT>, std::array<CharT, N>, std::span<CharT>, etc.
        return std::basic_string_view<CharT>{std::ranges::data(input), std::ranges::size(input)};
    }
}

} // namespace neo_cppjieba
