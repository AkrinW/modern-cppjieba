#pragma once

#include "Logging.hpp"
#include "Traits.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
namespace neo_cppjieba {
// Unicode decoding always checks input bounds and rejects malformed sequences.
// These checks remain enabled in release builds because input may come from users or files.

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

    [[nodiscard]] auto get_runes() const & noexcept -> const Unicode & {
        return runes;
    }
    auto get_runes() const && -> const Unicode & = delete;

    [[nodiscard]] auto get_offsets() const & noexcept -> const std::vector<uint32_t> & {
        return offsets;
    }
    auto get_offsets() const && -> const std::vector<uint32_t> & = delete;
};

/// A half-open range [begin, end) of rune positions within the input.
struct WordRange {
    uint32_t begin;
    uint32_t end;

    [[nodiscard]] constexpr auto size() const noexcept -> uint32_t {
        assert_check([this] { return begin <= end; }, "Reversed internal WordRange [{}, {})", begin, end);
        return end - begin;
    }

    /// Extract the corresponding sub-span from the original runes.
    [[nodiscard]] auto slice(std::span<const Rune> runes) const -> std::span<const Rune> {
        assert_check([&] { return end <= runes.size(); }, "Internal WordRange end {} exceeds {} runes", end,
                     runes.size());
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

// Replace runes and source offsets while retaining the destination buffers' capacity.
// Input must not alias either destination buffer. A failed decode leaves reusable, partial output.
template <StringLike T>
auto decode_with_offset_into(const T &input, UnicodeWithOffset &out) -> void;

// encode_one encodes a single Rune into a string of the target CharT encoding.
template <CharType CharT = char>
constexpr auto encode_one(Rune rune) -> std::basic_string<CharT>;

// encode encodes a sequence of Runes into a string of the target CharT encoding.
template <CharType CharT = char>
auto encode(std::span<const Rune> input) -> std::basic_string<CharT>;

// encodes a sequence of Unicode's WordRange [start, end) back to the source encoding using the provided offsets for
// fast lookup. it is faster than re-encoding each Rune when the target encoding matches the source encoding, as it can
// directly copy source code units. The source and offsets must come from the same validated input.
template <CharType CharT = char>
auto encode(std::basic_string_view<CharT> source, std::span<const uint32_t> offsets, WordRange range)
    -> std::basic_string<CharT>;

namespace detail {

// Distinguish malformed input from a successful decoding of the NUL character.
enum class UnicodeDecodeError : uint8_t {
    None,
    EmptyInput,
    TruncatedSequence,
    InvalidLeadingUnit,
    InvalidContinuation,
    OverlongSequence,
    Surrogate,
    OutOfRange
};

// One decoded scalar and its consumed source code units, or a specific input error.
struct DecodedRune {
    Rune rune{};
    uint8_t code_units{};
    UnicodeDecodeError error{UnicodeDecodeError::None};
};

// Exclude surrogate code points while retaining NUL, noncharacters and unassigned scalars.
[[nodiscard]] constexpr auto is_unicode_scalar(Rune rune) noexcept -> bool {
    return rune <= 0x10FFFF && (rune < 0xD800 || rune > 0xDFFF);
}

// Describe the input failure without allocating inside the decoding kernel.
constexpr auto decode_error_name(UnicodeDecodeError error) -> std::string_view {
    switch (error) {
        case UnicodeDecodeError::None:
            return "none";
        case UnicodeDecodeError::EmptyInput:
            return "empty input";
        case UnicodeDecodeError::TruncatedSequence:
            return "truncated sequence";
        case UnicodeDecodeError::InvalidLeadingUnit:
            return "invalid leading code unit";
        case UnicodeDecodeError::InvalidContinuation:
            return "invalid continuation code unit";
        case UnicodeDecodeError::OverlongSequence:
            return "overlong sequence";
        case UnicodeDecodeError::Surrogate:
            return "surrogate code point";
        case UnicodeDecodeError::OutOfRange:
            return "code point exceeds U+10FFFF";
    }
    assert_check([] { return false; }, "Unknown internal Unicode decoding error");
    std::unreachable();
}

// Report external decoding failures consistently at the public entry and within the decoding loop.
template <CodeUnit CharT>
inline auto throw_decode_error(UnicodeDecodeError error, size_t offset) -> void {
    constexpr auto encoding = encoding_of_v<CharT> == Encoding::UTF8    ? "UTF-8"
                              : encoding_of_v<CharT> == Encoding::UTF16 ? "UTF-16"
                                                                        : "UTF-32";
    check(false, "{} decoding failed at code-unit offset {}: {}", encoding, offset, decode_error_name(error));
}

// decode_one_utf8 decodes a single Unicode code point from a UTF-8 encoded string and returns it as a Rune.
template <CodeUnit CharT>
    requires(encoding_of_v<CharT> == Encoding::UTF8)
constexpr auto decode_one_utf8(std::span<const CharT> input) -> DecodedRune {
    assert_check([&] { return !input.empty(); }, "UTF-8 decoding requires a nonempty internal span");
    const auto first = static_cast<uint8_t>(input[0]);
    // one byte (ASCII)
    if (first < 0x80) {
        return {.rune = static_cast<Rune>(first), .code_units = 1};
    }
    // two bytes
    if (first >> 5 == 0x6) {
        if (input.size() < 2) {
            return {.error = UnicodeDecodeError::TruncatedSequence};
        }
        const auto second = static_cast<uint8_t>(input[1]);
        if (second >> 6 != 0x2) {
            return {.error = UnicodeDecodeError::InvalidContinuation};
        }
        const auto rune = static_cast<Rune>(((first & 0x1F) << 6) | (second & 0x3F));
        if (rune < 0x80) {
            return {.error = UnicodeDecodeError::OverlongSequence};
        }
        return {.rune = rune, .code_units = 2};
    }
    // three bytes
    if (first >> 4 == 0xE) {
        if (input.size() < 3) {
            return {.error = UnicodeDecodeError::TruncatedSequence};
        }
        const auto second = static_cast<uint8_t>(input[1]);
        const auto third = static_cast<uint8_t>(input[2]);
        if (second >> 6 != 0x2 || third >> 6 != 0x2) {
            return {.error = UnicodeDecodeError::InvalidContinuation};
        }
        const auto rune = static_cast<Rune>(((first & 0xF) << 12) | ((second & 0x3F) << 6) | (third & 0x3F));
        if (rune < 0x800) {
            return {.error = UnicodeDecodeError::OverlongSequence};
        }
        if (!is_unicode_scalar(rune)) {
            return {.error = UnicodeDecodeError::Surrogate};
        }
        return {.rune = rune, .code_units = 3};
    }
    // four bytes
    if (first < 0xF0 || first > 0xF4) {
        return {.error = UnicodeDecodeError::InvalidLeadingUnit};
    }
    if (input.size() < 4) {
        return {.error = UnicodeDecodeError::TruncatedSequence};
    }
    const auto second = static_cast<uint8_t>(input[1]);
    const auto third = static_cast<uint8_t>(input[2]);
    const auto fourth = static_cast<uint8_t>(input[3]);
    if (second >> 6 != 0x2 || third >> 6 != 0x2 || fourth >> 6 != 0x2) {
        return {.error = UnicodeDecodeError::InvalidContinuation};
    }
    const auto rune =
        static_cast<Rune>(((first & 0x7) << 18) | ((second & 0x3F) << 12) | ((third & 0x3F) << 6) | (fourth & 0x3F));
    if (rune < 0x10000) {
        return {.error = UnicodeDecodeError::OverlongSequence};
    }
    if (!is_unicode_scalar(rune)) {
        return {.error = UnicodeDecodeError::OutOfRange};
    }
    return {.rune = rune, .code_units = 4};
}

// decode_one_utf16 decodes a single Unicode code point from a UTF-16 encoded string and returns it as a Rune.
template <CharType CharT>
    requires(encoding_of_v<CharT> == Encoding::UTF16)
constexpr auto decode_one_utf16(std::span<const CharT> input) -> DecodedRune {
    assert_check([&] { return !input.empty(); }, "UTF-16 decoding requires a nonempty internal span");
    const auto first = static_cast<uint16_t>(input[0]);
    // single code unit (BMP)
    if (first < 0xD800 || first > 0xDFFF) {
        return {.rune = static_cast<Rune>(first), .code_units = 1};
    }
    // surrogate pair
    if (first > 0xDBFF) {
        return {.error = UnicodeDecodeError::Surrogate};
    }
    if (input.size() < 2) {
        return {.error = UnicodeDecodeError::TruncatedSequence};
    }
    const auto second = static_cast<uint16_t>(input[1]);
    if (second < 0xDC00 || second > 0xDFFF) {
        return {.error = UnicodeDecodeError::InvalidContinuation};
    }
    return {.rune = static_cast<Rune>(((first - 0xD800) << 10) | (second - 0xDC00)) + 0x10000, .code_units = 2};
}

// decode_one_utf32 decodes a single Unicode code point from a UTF-32 encoded string and returns it as a Rune.
template <CharType CharT>
    requires(encoding_of_v<CharT> == Encoding::UTF32)
constexpr auto decode_one_utf32(std::span<const CharT> input) -> DecodedRune {
    assert_check([&] { return !input.empty(); }, "UTF-32 decoding requires a nonempty internal span");
    const auto rune = static_cast<Rune>(input[0]);
    if (rune > 0x10FFFF) {
        return {.error = UnicodeDecodeError::OutOfRange};
    }
    if (!is_unicode_scalar(rune)) {
        return {.error = UnicodeDecodeError::Surrogate};
    }
    return {.rune = rune, .code_units = 1};
}

// Dispatch by encoding without reinterpreting pointers to distinct character types.
template <CodeUnit CharT>
constexpr auto decode_step(std::span<const CharT> input) -> DecodedRune {
    if constexpr (encoding_of_v<CharT> == Encoding::UTF8) {
        return decode_one_utf8(input);
    } else if constexpr (encoding_of_v<CharT> == Encoding::UTF16) {
        return decode_one_utf16(input);
    } else {
        return decode_one_utf32(input);
    }
}

// Keep exception construction off the successful path, including constant evaluation.
template <CodeUnit CharT>
constexpr auto checked_decode_step(std::span<const CharT> input, size_t offset) -> DecodedRune {
    const auto result = decode_step(input);
    if (result.error != UnicodeDecodeError::None) [[unlikely]] {
        throw_decode_error<CharT>(result.error, offset);
    }
    assert_check([&] { return result.code_units > 0 && result.code_units <= input.size(); },
                 "Unicode decoder produced invalid progress");
    return result;
}

// Typed storage for one encoded scalar; no cross-character pointer casts are needed.
template <CharType CharT>
struct EncodedRune {
    static constexpr auto capacity = encoding_of_v<CharT> == Encoding::UTF8    ? size_t{4}
                                     : encoding_of_v<CharT> == Encoding::UTF16 ? size_t{2}
                                                                               : size_t{1};
    std::array<CharT, capacity> units{};
    uint8_t size{};
};

// encode_one_utf8 encodes a single Rune into a UTF-8 sequence.
template <CharType CharT>
    requires(encoding_of_v<CharT> == Encoding::UTF8)
constexpr auto encode_one_utf8(Rune rune) -> EncodedRune<CharT> {
    if (rune < 0x80) {
        return {{static_cast<CharT>(rune), 0, 0, 0}, 1};
    }
    if (rune < 0x800) {
        return {{static_cast<CharT>(0xC0 | (rune >> 6)), static_cast<CharT>(0x80 | (rune & 0x3F)), 0, 0}, 2};
    }
    if (rune < 0x10000) {
        return {{static_cast<CharT>(0xE0 | (rune >> 12)), static_cast<CharT>(0x80 | ((rune >> 6) & 0x3F)),
                 static_cast<CharT>(0x80 | (rune & 0x3F)), 0},
                3};
    }
    // Invalid code points are rejected before this encoding kernel is called.
    return {{static_cast<CharT>(0xF0 | (rune >> 18)), static_cast<CharT>(0x80 | ((rune >> 12) & 0x3F)),
             static_cast<CharT>(0x80 | ((rune >> 6) & 0x3F)), static_cast<CharT>(0x80 | (rune & 0x3F))},
            4};
}

// encode_one_utf16 encodes a single Rune into a UTF-16 sequence.
template <CharType CharT>
    requires(encoding_of_v<CharT> == Encoding::UTF16)
constexpr auto encode_one_utf16(Rune rune) -> EncodedRune<CharT> {
    if (rune < 0x10000) {
        return {{static_cast<CharT>(rune), 0}, 1};
    }
    // Invalid code points are rejected before this encoding kernel is called.
    const auto adjusted = rune - 0x10000;
    return {{static_cast<CharT>(0xD800 + (adjusted >> 10)), static_cast<CharT>(0xDC00 + (adjusted & 0x3FF))}, 2};
}

// encode_one_utf32 encodes a single Rune into a UTF-32 code unit (identity).
template <CharType CharT>
    requires(encoding_of_v<CharT> == Encoding::UTF32)
constexpr auto encode_one_utf32(Rune rune) -> EncodedRune<CharT> {
    // Invalid code points are rejected before this encoding kernel is called.
    return {{static_cast<CharT>(rune)}, 1};
}

// Encode a validated scalar using storage of the requested output character type.
template <CharType CharT>
constexpr auto encode_step(Rune rune) -> EncodedRune<CharT> {
    assert_check([=] { return is_unicode_scalar(rune); }, "Invalid internal Unicode scalar");
    if constexpr (encoding_of_v<CharT> == Encoding::UTF8) {
        return encode_one_utf8<CharT>(rune);
    } else if constexpr (encoding_of_v<CharT> == Encoding::UTF16) {
        return encode_one_utf16<CharT>(rune);
    } else {
        return encode_one_utf32<CharT>(rune);
    }
}

// Public encoding accepts user-provided Rune values and reports their position on failure.
template <CharType CharT>
constexpr auto checked_encode_step(Rune rune, size_t index) -> EncodedRune<CharT> {
    if (!is_unicode_scalar(rune)) [[unlikely]] {
        check(false, "Invalid Unicode scalar U+{:X} at rune index {}", static_cast<uint32_t>(rune), index);
    }
    return encode_step<CharT>(rune);
}

// Copy source code units after range and offset validity has been established by the caller.
// Generated offsets and segmentation ranges are internal invariants; keep their diagnostics in debug builds.
template <CharType CharT>
inline auto encode_validated_source(std::basic_string_view<CharT> source, std::span<const uint32_t> offsets,
                                    WordRange range) -> std::basic_string<CharT> {
    assert_check([&] { return range.begin <= range.end && range.end < offsets.size(); },
                 "Invalid internal rune range [{}, {}) for {} Unicode offsets", range.begin, range.end, offsets.size());
    const auto begin = offsets[range.begin];
    const auto end = offsets[range.end];
    assert_check([&] { return begin <= end && end <= source.size(); },
                 "Invalid internal source offsets [{}, {}) for {} code units", begin, end, source.size());
    if (begin == end) {
        return {};
    }
    return std::basic_string<CharT>{source.data() + begin, end - begin};
}

// Check representability before narrowing positions or allocating the offset sentinel.
constexpr auto checked_offset_count(size_t source_size) -> size_t {
    if (source_size > std::numeric_limits<uint32_t>::max() || source_size == std::numeric_limits<size_t>::max())
        [[unlikely]] {
        check(false, "Source length {} exceeds the supported Unicode offset range", source_size);
    }
    return source_size + 1;
}

// Verify generated offsets once in debug builds, rather than rescanning them for every word.
inline auto valid_decoded_offsets(const UnicodeWithOffset &decoded, size_t source_size) -> bool {
    if (decoded.offsets.size() != decoded.runes.size() + 1 || decoded.offsets.empty() || decoded.offsets.front() != 0
        || decoded.offsets.back() != source_size) {
        return false;
    }
    return std::adjacent_find(decoded.offsets.begin(), decoded.offsets.end(),
                              [](uint32_t left, uint32_t right) { return left >= right; })
           == decoded.offsets.end();
}

// Compile out offset recording for callers that only need decoded runes.
enum class OffsetMode { Omit, Record };

// Decoding clears and may reallocate its output, so it cannot read from that same allocation.
template <CodeUnit CharT, typename Value>
auto overlaps_decode_buffer(std::span<const CharT> input, const std::vector<Value> &output) -> bool {
    if (input.empty() || output.empty()) {
        return false;
    }
    const auto less = std::less<const void *>{};
    return less(input.data(), output.data() + output.size()) && less(output.data(), input.data() + input.size());
}

// Share traversal and validation between both public decoding APIs.
template <OffsetMode Mode, CodeUnit CharT>
auto decode_into_impl(std::span<const CharT> input,
                      std::conditional_t<Mode == OffsetMode::Record, UnicodeWithOffset, Unicode> &result) -> void {
    auto &runes = [&]() -> Unicode & {
        if constexpr (Mode == OffsetMode::Record) {
            return result.runes;
        } else {
            return result;
        }
    }();
    assert_check([&] { return !overlaps_decode_buffer(input, runes); }, "Decoding input aliases its rune buffer");
    runes.clear();
    if constexpr (Mode == OffsetMode::Record) {
        assert_check([&] { return !overlaps_decode_buffer(input, result.offsets); },
                     "Decoding input aliases its offset buffer");
        result.offsets.clear();
        result.offsets.reserve(checked_offset_count(input.size()));
    }
    runes.reserve(input.size()); // reserve enough space to avoid multiple allocations

    for (auto i = size_t{0}; i < input.size();) {
        // Invalid UTF-8/UTF-16 sequences throw instead of discarding the decoded prefix.
        const auto decoded = checked_decode_step(input.subspan(i), i);
        if constexpr (Mode == OffsetMode::Record) {
            result.offsets.push_back(static_cast<uint32_t>(i));
        }
        runes.push_back(decoded.rune);
        i += decoded.code_units;
    }
    if constexpr (Mode == OffsetMode::Record) {
        result.offsets.push_back(static_cast<uint32_t>(input.size()));
        assert_check([&] { return valid_decoded_offsets(result, input.size()); }, "Invalid generated Unicode offsets");
    }
}

// One-shot decoding shares the same validation as callers that reuse their buffers.
template <OffsetMode Mode, CodeUnit CharT>
auto decode_impl(std::span<const CharT> input)
    -> std::conditional_t<Mode == OffsetMode::Record, UnicodeWithOffset, Unicode> {
    auto result = std::conditional_t<Mode == OffsetMode::Record, UnicodeWithOffset, Unicode>{};
    decode_into_impl<Mode>(input, result);
    return result;
}
} // namespace detail

template <CharType CharT>
auto WordRange::to_string(std::span<const Rune> runes) const -> std::basic_string<CharT> {
    return encode<CharT>(slice(runes));
}

template <StringLike T>
constexpr auto decode_one(const T &input) -> Rune {
    const auto units = as_code_units(input);
    if (units.empty()) [[unlikely]] {
        detail::throw_decode_error<resolve_char_type_t<T>>(detail::UnicodeDecodeError::EmptyInput, 0);
    }
    return detail::checked_decode_step(units, 0).rune;
}

template <StringLike T>
inline auto decode(const T &input) -> Unicode {
    return detail::decode_impl<detail::OffsetMode::Omit>(as_code_units(input));
}

template <StringLike T>
inline auto decode_with_offset(const T &input) -> UnicodeWithOffset {
    return detail::decode_impl<detail::OffsetMode::Record>(as_code_units(input));
}

template <StringLike T>
inline auto decode_with_offset_into(const T &input, UnicodeWithOffset &out) -> void {
    detail::decode_into_impl<detail::OffsetMode::Record>(as_code_units(input), out);
}

template <CharType CharT>
constexpr auto encode_one(Rune rune) -> std::basic_string<CharT> {
    const auto encoded = detail::checked_encode_step<CharT>(rune, 0);
    return std::basic_string<CharT>{encoded.units.data(), encoded.size};
}

template <CharType CharT>
inline auto encode(std::span<const Rune> input) -> std::basic_string<CharT> {
    auto result = std::basic_string<CharT>{};
    result.reserve(input.size()); // at least one code unit per Rune

    for (auto i = size_t{0}; i < input.size(); ++i) {
        // Invalid code points are reported rather than converted to an empty string.
        const auto encoded = detail::checked_encode_step<CharT>(input[i], i);
        result.append(encoded.units.data(), encoded.size);
    }
    return result;
}

template <CharType CharT>
inline auto encode(std::basic_string_view<CharT> source, std::span<const uint32_t> offsets, WordRange range)
    -> std::basic_string<CharT> {
    check(range.begin <= range.end && range.end < offsets.size(), "Invalid rune range [{}, {}) for {} Unicode offsets",
          range.begin, range.end, offsets.size());
    const auto begin = offsets[range.begin];
    const auto end = offsets[range.end];
    check(begin <= end && end <= source.size(), "Invalid source offsets [{}, {}) for {} code units", begin, end,
          source.size());
    return detail::encode_validated_source(source, offsets, range);
}

} // namespace neo_cppjieba
