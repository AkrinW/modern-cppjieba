#pragma once

#include "Logging.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
namespace neo_cppjieba {
static_assert(std::numeric_limits<unsigned char>::digits == 8, "Unicode byte input requires 8-bit bytes");

// Byte buffers carry UTF-8 octets, independent of signedness and native byte order.
template <typename T>
concept ByteType = std::same_as<T, std::byte> || std::same_as<T, unsigned char> || std::same_as<T, signed char>;

// Encoding represents the encoding type of a string.
enum class Encoding : uint8_t { UTF8, UTF16, UTF32 };

// encoding_of is a type trait that maps a character type to its corresponding encoding.
template <typename T>
struct encoding_of;

// Byte storage uses the same UTF-8 decoder as char and char8_t.
template <ByteType T>
struct encoding_of<T> {
    static constexpr auto value = Encoding::UTF8;
};

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

// Wide input must contain UTF-16 or UTF-32 code units according to its width, independently of the locale.
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

// Input code units also include byte storage; output strings retain standard character types.
template <typename T>
concept CodeUnit = CharType<T> || ByteType<T>;

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

// Require the same const access and readable code-unit pointer used by as_view.
template <typename T>
concept ConstCodeUnitRange =
    CodeUnit<resolve_char_type_t<T>> && std::ranges::contiguous_range<const std::remove_reference_t<T>>
    && std::ranges::sized_range<const std::remove_reference_t<T>> && requires(const std::remove_reference_t<T> &input) {
           { std::ranges::data(input) } -> std::convertible_to<const resolve_char_type_t<T> *>;
           { std::ranges::size(input) } -> std::convertible_to<std::size_t>;
       };

} // namespace detail

// Core StringLike Concept: any type convertible to a basic_string_view via as_view.
//  - Must have a valid CodeUnit type.
//  - Characters may convert to string_view; byte storage must be a contiguous range with a known size.
template <typename T>
concept StringLike =
    CodeUnit<resolve_char_type_t<T>>
    && (!std::is_array_v<std::remove_reference_t<T>> || std::is_bounded_array_v<std::remove_reference_t<T>>)
    && ((CharType<resolve_char_type_t<T>>
         && std::convertible_to<const std::remove_reference_t<T> &, std::basic_string_view<resolve_char_type_t<T>>>)
        || detail::ConstCodeUnitRange<T>);

// Byte input produces UTF-8 std::string output; native character input preserves its character type.
template <StringLike T>
using output_char_type_t = std::conditional_t<ByteType<resolve_char_type_t<T>>, char, resolve_char_type_t<T>>;

// Owning temporaries may be decoded, but must not expose a view that outlives them.
template <typename T>
concept StringViewSource =
    StringLike<T>
    && (std::is_lvalue_reference_v<T> || std::ranges::borrowed_range<T> || std::is_pointer_v<std::remove_cvref_t<T>>);

// Unified input adaptation for as_code_units and as_view.
// Character arrays omit one trailing NUL; byte buffers and sized ranges retain every code unit, including NULs.
// Pointer inputs must refer to a readable, NUL-terminated C string.
template <StringViewSource T>
constexpr auto as_code_units(T &&input) -> std::span<const resolve_char_type_t<T>> {
    using CharT = resolve_char_type_t<T>;
    const auto &source = input;

    if constexpr (ByteType<CharT>) {
        return {std::ranges::data(source), std::ranges::size(source)};
    } else if constexpr (std::is_array_v<std::remove_reference_t<T>>) {
        constexpr auto extent = std::extent_v<std::remove_reference_t<T>>;
        const auto size = extent - (source[extent - 1] == CharT{} ? 1 : 0);
        return {source, size};
    } else if constexpr (std::is_pointer_v<std::remove_cvref_t<T>>) {
        if (source == nullptr) [[unlikely]] {
            check(false, "Cannot view a null C-string pointer");
        }
        const auto view = std::basic_string_view<CharT>{source};
        return {view.data(), view.size()};
    } else if constexpr (detail::ConstCodeUnitRange<T>) {
        // Branch B: sized strings, views, std::vector<CharT>, std::array<CharT, N>, std::span<CharT>, etc.
        return {std::ranges::data(source), std::ranges::size(source)};
    } else {
        // Branch A: custom types with a const conversion to string_view.
        const auto view = std::basic_string_view<CharT>{source};
        return {view.data(), view.size()};
    }
}

// Expose a string view without copying; char may inspect the representation of any byte type.
// Byte-to-char views require runtime evaluation in C++23; as_code_units also supports constant evaluation.
template <StringViewSource T>
constexpr auto as_view(T &&input) -> std::basic_string_view<output_char_type_t<T>> {
    const auto units = as_code_units(std::forward<T>(input));
    if constexpr (ByteType<resolve_char_type_t<T>>) {
        return {reinterpret_cast<const char *>(units.data()), units.size()};
    } else {
        return {units.data(), units.size()};
    }
}

} // namespace neo_cppjieba
