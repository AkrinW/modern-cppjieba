#pragma once

#include "neo/Traits.hpp"
#include "neo/detail/Logging.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace neo_cppjieba {

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
    [[nodiscard]] auto get_runes() const && -> const Unicode & = delete;

    [[nodiscard]] auto get_offsets() const & noexcept -> const std::vector<uint32_t> & {
        return offsets;
    }
    [[nodiscard]] auto get_offsets() const && -> const std::vector<uint32_t> & = delete;
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

    // Include Unicode.hpp for this encoding operation.
    template <CharType CharT>
    [[nodiscard]] auto to_string(std::span<const Rune> runes) const -> std::basic_string<CharT>;
};

} // namespace neo_cppjieba
