#pragma once

#include "neo/Config.hpp"
#include "neo/Traits.hpp"
#include "neo/UnicodeTypes.hpp"
#include "neo/detail/Unicode.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace neo_cppjieba {

// Unicode decoding always checks input bounds and rejects malformed sequences.
// These checks remain enabled in release builds because input may come from users or files.

// Validate the UTF encoding associated with the input's code-unit type without allocating decoding buffers.
// This checks well-formedness, not the original encoding of bytes that are valid in several encodings.
template <StringLike T>
[[nodiscard]] constexpr auto is_valid_utf(const T &input) -> bool {
    const auto units = as_code_units(input);
    for (auto offset = std::size_t{0}; offset < units.size();) {
        const auto decoded = detail::decode_step(units.subspan(offset));
        if (decoded.error != detail::UnicodeDecodeError::None) {
            return false;
        }
        offset += decoded.code_units;
    }
    return true;
}

// decode_one decodes a single Unicode code point from the input string and returns it as a Rune.
template <StringLike T>
constexpr auto decode_one(const T &input) -> Rune {
    return detail::decode_one_impl(as_code_units(input));
}

// decode decodes an input string into a sequence of Unicode code points and returns it as a Unicode.
// T can be a std::string, std::string_view, char*, char[N], etc. — any StringLike type.
template <StringLike T>
inline auto decode(const T &input) -> Unicode {
    return detail::decode_impl<detail::OffsetMode::Omit>(as_code_units(input));
}

// decode_with_offset decodes an input string into Unicode while recording source offsets
// for each code point, enabling fast re-encoding via UnicodeWithOffset.
template <StringLike T>
inline auto decode_with_offset(const T &input) -> UnicodeWithOffset {
    return detail::decode_impl<detail::OffsetMode::Record>(as_code_units(input));
}

// Replace runes and source offsets while retaining the destination buffers' capacity.
// Input must not alias either destination buffer. A failed decode leaves reusable, partial output.
template <StringLike T>
inline auto decode_with_offset_into(const T &input, UnicodeWithOffset &out) -> void {
    detail::decode_into_impl<detail::OffsetMode::Record>(as_code_units(input), out);
}

// encode_one encodes a single Rune into a string of the target CharT encoding.
template <CharType CharT = char>
constexpr auto encode_one(Rune rune) -> std::basic_string<CharT> {
    return detail::encode_one_impl<CharT>(rune);
}

// encode encodes a sequence of Runes into a string of the target CharT encoding.
template <CharType CharT = char>
inline auto encode(std::span<const Rune> input) -> std::basic_string<CharT> {
    return detail::encode_impl<CharT>(input);
}

// encodes a sequence of Unicode's WordRange [start, end) back to the source encoding using the provided offsets for
// fast lookup. it is faster than re-encoding each Rune when the target encoding matches the source encoding, as it can
// directly copy source code units. The source and offsets must come from the same validated input.
template <CharType CharT = char>
inline auto encode(std::basic_string_view<CharT> source, std::span<const SourceOffset> offsets, WordRange range)
    -> std::basic_string<CharT> {
    return detail::encode_source_impl(source, offsets, range);
}

template <CharType CharT>
auto WordRange::to_string(std::span<const Rune> runes) const -> std::basic_string<CharT> {
    return encode<CharT>(slice(runes));
}

} // namespace neo_cppjieba
