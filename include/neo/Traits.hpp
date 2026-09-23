#pragma once

#include "Logging.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
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
    static_assert(sizeof(wchar_t) == 2 || sizeof(wchar_t) == 4, "Unsupported wchar_t width");
    static constexpr auto value = sizeof(wchar_t) == 2 ? Encoding::UTF16 : Encoding::UTF32;
};

// encoding_of_v is a helper variable template that provides a convenient way to access the encoding type for a given
// character type.
template <typename T>
    requires requires { encoding_of<std::remove_cvref_t<T>>::value; }
inline constexpr auto encoding_of_v = encoding_of<std::remove_cvref_t<T>>::value;

// ── Concepts & as_view ─────────────────────────────────────────────────
/// CharType: a character type whose encoding is known.
template <typename T>
concept CharType = std::same_as<T, char> || std::same_as<T, char8_t> || std::same_as<T, char16_t>
                   || std::same_as<T, char32_t> || std::same_as<T, wchar_t>;

// Use a consteval function and if constexpr to replace complex SFINAE template specializations,
// avoiding ambiguity during type extraction.
template <typename T>
consteval auto get_char_type() {
    using Decayed = std::decay_t<T>;
    if constexpr (std::is_pointer_v<Decayed>) {
        // Pointer or decayed C-array (e.g., const char*, char[N])
        return std::type_identity<std::remove_cv_t<std::remove_pointer_t<Decayed>>>{};
    } else if constexpr (std::ranges::contiguous_range<const std::remove_reference_t<T>>) {
        // C++20 contiguous memory range (e.g., std::string, std::vector, std::span, std::array)
        return std::type_identity<std::remove_cv_t<std::ranges::range_value_t<const std::remove_reference_t<T>>>>{};
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

namespace detail {

// Require the same const access and readable character pointer used by as_view.
template <typename T>
concept ConstCharacterRange =
    CharType<resolve_char_type_t<T>> && std::ranges::contiguous_range<const std::remove_reference_t<T>>
    && std::ranges::sized_range<const std::remove_reference_t<T>> && requires(const std::remove_reference_t<T> &input) {
           { std::ranges::data(input) } -> std::convertible_to<const resolve_char_type_t<T> *>;
           { std::ranges::size(input) } -> std::convertible_to<std::size_t>;
       };

} // namespace detail

// Core StringLike Concept: any type convertible to a basic_string_view via as_view.
//  - Must have a valid CharType.
//  - Must be implicitly convertible to string_view, OR be a contiguous range with a known size.
template <typename T>
concept StringLike =
    CharType<resolve_char_type_t<T>>
    && (!std::is_array_v<std::remove_reference_t<T>> || std::is_bounded_array_v<std::remove_reference_t<T>>)
    && (std::convertible_to<const std::remove_reference_t<T> &, std::basic_string_view<resolve_char_type_t<T>>>
        || detail::ConstCharacterRange<T>);

// Owning temporaries may be decoded, but must not expose a view that outlives them.
template <typename T>
concept StringViewSource =
    StringLike<T>
    && (std::is_lvalue_reference_v<T> || std::ranges::borrowed_range<T> || std::is_pointer_v<std::remove_cvref_t<T>>);

// Unified as_view implementation
// Arrays omit one trailing NUL; sized ranges retain every code unit, including NULs.
// Pointer inputs must refer to a readable, NUL-terminated C string.
template <StringViewSource T>
constexpr auto as_view(T &&input) -> std::basic_string_view<resolve_char_type_t<T>> {
    using CharT = resolve_char_type_t<T>;
    const auto &source = input;

    if constexpr (std::is_array_v<std::remove_reference_t<T>>) {
        constexpr auto extent = std::extent_v<std::remove_reference_t<T>>;
        const auto size = extent - (source[extent - 1] == CharT{} ? 1 : 0);
        return std::basic_string_view<CharT>{source, size};
    } else if constexpr (std::is_pointer_v<std::remove_cvref_t<T>>) {
        if (source == nullptr) [[unlikely]] {
            check(false, "Cannot view a null C-string pointer");
        }
        return std::basic_string_view<CharT>{source};
    } else if constexpr (detail::ConstCharacterRange<T>) {
        // Branch B: sized strings, views, std::vector<CharT>, std::array<CharT, N>, std::span<CharT>, etc.
        return std::basic_string_view<CharT>{std::ranges::data(source), std::ranges::size(source)};
    } else {
        // Branch A: custom types with a const conversion to string_view.
        return std::basic_string_view<CharT>{source};
    }
}

} // namespace neo_cppjieba
