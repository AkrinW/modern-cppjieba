#pragma once

#include "Traits.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace neo_cppjieba {
// SAFE_STRING_CHECK is a compile-time constant that indicates whether to perform safety checks on input strings during
// decoding. Setting it to TRUE enables checks for valid UTF-8/UTF-16 sequences, while FALSE may skip these checks for
// performance at the risk of undefined behavior on invalid input.
inline constexpr auto SAFE_STRING_CHECK = true;

// Rune is a Unicode code point, every single Unicode character is represented by a Rune. using char32_t to fixed width
// of Rune to 4 bytes, which can represent all Unicode code points.
using Rune = char32_t;
// Unicode is a sequence of Runes. an input string will be decoding to Unicode.
using Unicode = std::vector<Rune>;

// UnicodeWithOffset stores decoded Unicode along with source string offset information.
// When the target encoding matches the source encoding, re-encoding can be done by
// simply copying bytes from the original source, avoiding the per-codepoint encoding cost.
struct UnicodeWithOffset {
    Unicode runes;
    // offsets[i] is the code-unit offset of runes[i] in source.
    // offsets[runes.size()] == source.size()  (sentinel for easy range computation).
    // Total size: runes.size() + 1.
    std::vector<uint32_t> offsets;

    [[nodiscard]] auto get_runes() const -> const Unicode & {
        return runes;
    }

    [[nodiscard]] auto get_offsets() const -> const std::vector<uint32_t> & {
        return offsets;
    }
};

/// A half-open range [begin, end) of rune positions within the input.
struct WordRange {
    uint32_t begin;
    uint32_t end;

    [[nodiscard]] constexpr auto size() const noexcept -> uint32_t {
        return end - begin;
    }

    /// Extract the corresponding sub-span from the original runes.
    [[nodiscard]] auto slice(std::span<const Rune> runes) const -> std::span<const Rune> {
        return runes.subspan(begin, size());
    }

    constexpr auto operator==(const WordRange &) const noexcept -> bool = default;

    template <CharType CharT>
    [[nodiscard]] auto to_string(std::span<const Rune> runes) const -> std::basic_string<CharT>;
};

// decode_one decodes a single Unicode code point from the input string and returns it as a Rune.
template <StringLike T>
constexpr auto decode_one(const T &input) -> Rune;

// decode decodes an input string into a sequence of Unicode code points and returns it as a Unicode.
// T can be a std::string, std::string_view, char*, char[N], etc. — any StringLike type.
template <StringLike T>
auto decode(const T &input) -> Unicode;

// decode_with_offset decodes an input string into Unicode while recording source offsets
// for each code point, enabling fast re-encoding via UnicodeWithOffset.
template <StringLike T>
auto decode_with_offset(const T &input) -> UnicodeWithOffset;

// encode_one encodes a single Rune into a string of the target CharT encoding.
template <CharType CharT = char>
constexpr auto encode_one(Rune rune) -> std::basic_string<CharT>;

// encode encodes a sequence of Runes into a string of the target CharT encoding.
template <CharType CharT = char>
auto encode(std::span<const Rune> input) -> std::basic_string<CharT>;

// encodes a sequence of Unicode's WordRange [start, end) back to the source encoding using the provided offsets for
// fast lookup. it is faster than re-encoding each Rune when the target encoding matches the source encoding, as it can
// directly copy the corresponding byte range from the original source string.
template <CharType CharT = char>
auto encode(std::basic_string_view<CharT> source, std::span<const uint32_t> offsets, WordRange range)
    -> std::basic_string<CharT>;

namespace detail {
// decode_one_utf8 decodes a single Unicode code point from a UTF-8 encoded string and returns it as a Rune.
constexpr auto decode_one_utf8(const std::string_view &input) -> std::pair<Rune, uint8_t> {
    if (input.empty()) {
        return std::pair{Rune{}, 0};
    }
    auto &&first = static_cast<unsigned char>(input[0]);
    // one byte (ASCII)
    if (first < 0x80) {
        return std::pair{static_cast<Rune>(first), 1};
    }
    // two bytes
    if (first >> 5 == 0x6) {
        if constexpr (SAFE_STRING_CHECK) {
            if (input.size() < 2) {
                return std::pair{Rune{}, 0};
            }
        }
        auto &&second = static_cast<unsigned char>(input[1]);
        if constexpr (SAFE_STRING_CHECK) {
            if (second >> 6 != 0x2) {
                return std::pair{Rune{}, 0};
            }
        }
        return std::pair{static_cast<Rune>(((first & 0x1F) << 6) | (second & 0x3F)), 2};
    }
    // three bytes
    if (first >> 4 == 0xE) {
        if constexpr (SAFE_STRING_CHECK) {
            if (input.size() < 3) {
                return std::pair{Rune{}, 0};
            }
        }
        auto &&second = static_cast<unsigned char>(input[1]);
        auto &&third = static_cast<unsigned char>(input[2]);
        if constexpr (SAFE_STRING_CHECK) {
            if (second >> 6 != 0x2 || third >> 6 != 0x2) {
                return std::pair{Rune{}, 0};
            }
        }
        return std::pair{static_cast<Rune>(((first & 0xF) << 12) | ((second & 0x3F) << 6) | (third & 0x3F)), 3};
    }
    // four bytes
    if constexpr (SAFE_STRING_CHECK) {
        if (first >> 3 != 0x1E) {
            return std::pair{Rune{}, 0};
        }
    }
    if constexpr (SAFE_STRING_CHECK) {
        if (input.size() < 4) {
            return std::pair{Rune{}, 0};
        }
    }
    auto &&second = static_cast<unsigned char>(input[1]);
    auto &&third = static_cast<unsigned char>(input[2]);
    auto &&fourth = static_cast<unsigned char>(input[3]);
    if constexpr (SAFE_STRING_CHECK) {
        if (second >> 6 != 0x2 || third >> 6 != 0x2 || fourth >> 6 != 0x2) {
            return std::pair{Rune{}, 0};
        }
    }
    return std::pair{
        static_cast<Rune>(((first & 0x7) << 18) | ((second & 0x3F) << 12) | ((third & 0x3F) << 6) | (fourth & 0x3F)),
        4};
}

// decode_one_utf16 decodes a single Unicode code point from a UTF-16 encoded string and returns it as a Rune.
constexpr auto decode_one_utf16(const std::u16string_view &input) -> std::pair<Rune, uint8_t> {
    if (input.empty()) {
        return std::pair{Rune{}, 0};
    }
    auto &&first = input[0];
    // single code unit (BMP)
    if (first < 0xD800 || first > 0xDFFF) {
        return std::pair{static_cast<Rune>(first), 1};
    }
    // surrogate pair
    if constexpr (SAFE_STRING_CHECK) {
        if (first > 0xDBFF) {
            return std::pair{Rune{}, 0};
        }
    }
    if constexpr (SAFE_STRING_CHECK) {
        if (input.size() < 2) {
            return std::pair{Rune{}, 0};
        }
    }
    auto &&second = input[1];
    if constexpr (SAFE_STRING_CHECK) {
        if (second < 0xDC00 || second > 0xDFFF) {
            return std::pair{Rune{}, 0};
        }
    }
    return std::pair{static_cast<Rune>(((first - 0xD800) << 10) | (second - 0xDC00)) + 0x10000, 2};
}

// decode_one_utf32 decodes a single Unicode code point from a UTF-32 encoded string and returns it as a Rune.
constexpr auto decode_one_utf32(const std::u32string_view &input) -> std::pair<Rune, uint8_t> {
    if (input.empty()) {
        return std::pair{Rune{}, 0};
    }
    return std::pair{static_cast<Rune>(input[0]), 1};
}
// encode_one_utf8 encodes a single Rune into a UTF-8 sequence.
constexpr auto encode_one_utf8(Rune rune) -> std::pair<std::array<char, 4>, uint8_t> {
    if (rune < 0x80) {
        return std::pair{std::array<char, 4>{static_cast<char>(rune), 0, 0, 0}, 1};
    }
    if (rune < 0x800) {
        return std::pair{
            std::array<char, 4>{static_cast<char>(0xC0 | (rune >> 6)), static_cast<char>(0x80 | (rune & 0x3F)), 0, 0},
            2};
    }
    if (rune < 0x10000) {
        return std::pair{std::array<char, 4>{static_cast<char>(0xE0 | (rune >> 12)),
                                             static_cast<char>(0x80 | ((rune >> 6) & 0x3F)),
                                             static_cast<char>(0x80 | (rune & 0x3F)), 0},
                         3};
    }
    if (rune <= 0x10FFFF) {
        return std::pair{std::array<char, 4>{
                             static_cast<char>(0xF0 | (rune >> 18)), static_cast<char>(0x80 | ((rune >> 12) & 0x3F)),
                             static_cast<char>(0x80 | ((rune >> 6) & 0x3F)), static_cast<char>(0x80 | (rune & 0x3F))},
                         4};
    }
    return std::pair{std::array<char, 4>{}, 0}; // invalid code point
}

// encode_one_utf16 encodes a single Rune into a UTF-16 sequence.
constexpr auto encode_one_utf16(Rune rune) -> std::pair<std::array<char16_t, 2>, uint8_t> {
    if (rune < 0x10000) {
        return std::pair{std::array<char16_t, 2>{static_cast<char16_t>(rune), 0}, 1};
    }
    if (rune <= 0x10FFFF) {
        auto adjusted = rune - 0x10000;
        return std::pair{std::array<char16_t, 2>{static_cast<char16_t>(0xD800 + (adjusted >> 10)),
                                                 static_cast<char16_t>(0xDC00 + (adjusted & 0x3FF))},
                         2};
    }
    return std::pair{std::array<char16_t, 2>{}, 0}; // invalid code point
}

// encode_one_utf32 encodes a single Rune into a UTF-32 code unit (identity).
constexpr auto encode_one_utf32(Rune rune) -> std::pair<char32_t, uint8_t> {
    if (rune <= 0x10FFFF) {
        return std::pair{rune, 1};
    }
    return std::pair{char32_t{}, 0}; // invalid code point
}
} // namespace detail

template <CharType CharT>
auto WordRange::to_string(std::span<const Rune> runes) const -> std::basic_string<CharT> {
    return encode<CharT>(slice(runes));
}

template <StringLike T>
constexpr auto decode_one(const T &input) -> Rune {
    auto view = as_view(input);
    using CharT = typename decltype(view)::value_type;

    if constexpr (encoding_of_v<CharT> == Encoding::UTF8) {
        return detail::decode_one_utf8(std::string_view{view.data(), view.size()}).first;
    } else if constexpr (encoding_of_v<CharT> == Encoding::UTF16) {
        return detail::decode_one_utf16(std::u16string_view{view.data(), view.size()}).first;
    } else {
        return detail::decode_one_utf32(std::u32string_view{view.data(), view.size()}).first;
    }
}

template <StringLike T>
inline auto decode(const T &input) -> Unicode {
    auto view = as_view(input);
    using CharT = typename decltype(view)::value_type;
    auto result = Unicode{};
    result.reserve(view.size()); // reserve enough space to avoid multiple allocations

    if constexpr (encoding_of_v<CharT> == Encoding::UTF8) {
        auto i = size_t{0};
        while (i < view.size()) {
            auto &&[rune, size] = detail::decode_one_utf8(std::string_view{view.data() + i, view.size() - i});
            if (size == 0) {
                result.clear();
                break; // invalid UTF-8 sequence
            }
            result.push_back(rune);
            i += size;
        }
    } else if constexpr (encoding_of_v<CharT> == Encoding::UTF16) {
        auto i = size_t{0};
        while (i < view.size()) {
            auto &&[rune, size] = detail::decode_one_utf16(std::u16string_view{view.data() + i, view.size() - i});
            if (size == 0) {
                result.clear();
                break; // invalid UTF-16 sequence
            }
            result.push_back(rune);
            i += size;
        }
    } else {
        for (const auto &ch : view) {
            result.push_back(static_cast<Rune>(ch));
        }
    }
    return result;
}

template <StringLike T>
inline auto decode_with_offset(const T &input) -> UnicodeWithOffset {
    using CharT = resolve_char_type_t<T>;
    auto view = as_view(input);
    auto result = UnicodeWithOffset{};
    result.runes.reserve(view.size());
    result.offsets.reserve(view.size() + 1);

    if constexpr (encoding_of_v<CharT> == Encoding::UTF8) {
        auto i = uint32_t{0};
        while (i < view.size()) {
            result.offsets.push_back(i);
            auto &&[rune, size] = detail::decode_one_utf8(std::string_view{view.data() + i, view.size() - i});
            if (size == 0) {
                result.runes.clear();
                result.offsets.clear();
                result.offsets.push_back(0);
                return result;
            }
            result.runes.push_back(rune);
            i += size;
        }
    } else if constexpr (encoding_of_v<CharT> == Encoding::UTF16) {
        auto i = uint32_t{0};
        while (i < view.size()) {
            result.offsets.push_back(i);
            auto &&[rune, size] = detail::decode_one_utf16(std::u16string_view{view.data() + i, view.size() - i});
            if (size == 0) {
                result.runes.clear();
                result.offsets.clear();
                result.offsets.push_back(0);
                return result;
            }
            result.runes.push_back(rune);
            i += size;
        }
    } else {
        for (auto i = size_t{0}; i < view.size(); ++i) {
            result.offsets.push_back(i);
            result.runes.push_back(static_cast<Rune>(view[i]));
        }
    }

    result.offsets.push_back(view.size());
    return result;
}

template <CharType CharT>
constexpr auto encode_one(Rune rune) -> std::basic_string<CharT> {
    if constexpr (encoding_of_v<CharT> == Encoding::UTF8) {
        auto &&[buf, len] = detail::encode_one_utf8(rune);
        return std::basic_string<CharT>{buf.data(), len};
    } else if constexpr (encoding_of_v<CharT> == Encoding::UTF16) {
        auto &&[buf, len] = detail::encode_one_utf16(rune);
        return std::basic_string<CharT>{reinterpret_cast<const CharT *>(buf.data()), len};
    } else {
        auto &&[val, len] = detail::encode_one_utf32(rune);
        return std::basic_string<CharT>{reinterpret_cast<const CharT *>(&val), len};
    }
}

template <CharType CharT>
inline auto encode(std::span<const Rune> input) -> std::basic_string<CharT> {
    auto result = std::basic_string<CharT>{};
    result.reserve(input.size()); // at least one code unit per Rune

    if constexpr (encoding_of_v<CharT> == Encoding::UTF8) {
        for (auto rune : input) {
            auto &&[buf, len] = detail::encode_one_utf8(rune);
            if (len == 0) {
                result.clear();
                break; // invalid code point
            }
            result.append(buf.data(), len);
        }
    } else if constexpr (encoding_of_v<CharT> == Encoding::UTF16) {
        for (auto rune : input) {
            auto &&[buf, len] = detail::encode_one_utf16(rune);
            if (len == 0) {
                result.clear();
                break; // invalid code point
            }
            result.append(reinterpret_cast<const CharT *>(buf.data()), len);
        }
    } else {
        for (auto rune : input) {
            result.push_back(static_cast<CharT>(rune));
        }
    }
    return result;
}

template <CharType CharT>
inline auto encode(std::basic_string_view<CharT> source, std::span<const uint32_t> offsets, WordRange range)
    -> std::basic_string<CharT> {
    return std::basic_string<CharT>{source.data() + offsets[range.begin], source.data() + offsets[range.end]};
}

} // namespace neo_cppjieba
