#pragma once

#include "neo/Token.hpp"
#include "neo/Traits.hpp"
#include "neo/Unicode.hpp"
#include "neo/detail/DictTrie.hpp"
#include "neo/detail/FullSegment.hpp"
#include "neo/detail/HMMSegment.hpp"
#include "neo/detail/HMModel.hpp"
#include "neo/detail/Logging.hpp"
#include "neo/detail/MPSegment.hpp"
#include "neo/detail/MixSegment.hpp"
#include "neo/detail/QuerySegment.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace neo_cppjieba {

// Each mode names a complete behavior, including the modes that disable HMM.
enum class CutMode : uint8_t { MIX, MIX_NO_HMM, FULL, SEARCH, SEARCH_NO_HMM, HMM, MP };

namespace detail {

// Public entry points accept only named segmentation modes.
constexpr auto valid_cut_mode(CutMode mode) noexcept -> bool {
    switch (mode) {
        case CutMode::MIX:
        case CutMode::MIX_NO_HMM:
        case CutMode::FULL:
        case CutMode::SEARCH:
        case CutMode::SEARCH_NO_HMM:
        case CutMode::HMM:
        case CutMode::MP:
            return true;
    }
    return false;
}

// Mode values supplied by callers are configuration errors, not internal invariants.
inline auto check_cut_mode(CutMode mode) -> void {
    check(valid_cut_mode(mode), "Unknown Jieba cut mode {}", std::to_underlying(mode));
}

// Partition modes cover every rune once; full and search modes may emit overlapping words.
[[nodiscard]] inline auto valid_jieba_result(CutMode mode, std::span<const WordRange> words, size_t rune_count)
    -> bool {
    if (mode == CutMode::MIX || mode == CutMode::MIX_NO_HMM || mode == CutMode::HMM || mode == CutMode::MP) {
        return valid_segment_partition(words, rune_count);
    }
    if (rune_count == 0) {
        return words.empty();
    }
    return !words.empty() && std::ranges::all_of(words, [rune_count](const WordRange &word) {
        return word.begin < word.end && word.end <= rune_count;
    });
}

} // namespace detail

/// Jieba is the top-level facade for Chinese word segmentation.
///
/// It supports two usage styles:
///
///   1. Direct string input: pass any StringLike input and get
///      std::vector<std::basic_string<CharT>> back.
///
///   2. Manual Unicode pipeline: decode to Unicode or UnicodeWithOffset, cut to
///      WordRange, then encode manually. The UnicodeWithOffset path can reuse
///      source offsets for faster re-encoding.
// These usage notes describe the former facade. Named output operations now distinguish
// borrowed tokens, owned text, strings, and ranges; Unicode utilities live in Unicode.hpp.
class Jieba {
public:
    // An empty user dictionary path explicitly disables user dictionary loading.
    explicit Jieba(std::string_view dict_path, std::string_view model_path, std::string_view user_dict_path)
        : dict_(dict_path, user_dict_path), model_(model_path) {
    }

    // Borrowing rejects owning temporaries; callers keep the original text alive and stable.
    template <StringViewSource Input>
    [[nodiscard]] auto cut(Input &&input, CutMode mode) const -> Tokens<output_char_type_t<Input>> {
        const auto source = as_view(std::forward<Input>(input));
        auto positions = std::vector<TokenPosition>{};
        auto decoded = UnicodeWithOffset{};
        cut_into(source, mode, positions, decoded);
        return Tokens<output_char_type_t<Input>>{source, std::move(positions)};
    }

    // Replaces output while retaining its capacity. Output records contain no decoding-buffer pointers.
    // Caller-owned decoding buffers can be reused across inputs; results never borrow this storage.
    // Input must not alias the output array or either decoding buffer.
    template <StringLike Input, typename Output>
        requires(std::same_as<Output, SourceRange> || std::same_as<Output, TokenPosition>
                 || std::same_as<Output, std::basic_string<output_char_type_t<Input>>>)
    auto cut_into(const Input &input, CutMode mode, std::vector<Output> &out, UnicodeWithOffset &decoded) const
        -> void {
        out.clear();
        const auto source = as_view(input);
        const auto ranges = prepare_and_cut(source, mode, decoded);
        out.reserve(ranges.size());
        for (const auto &range : ranges) {
            const auto source_range = to_source_range(range, decoded.offsets);
            if constexpr (std::same_as<Output, SourceRange>) {
                out.push_back(source_range);
            } else if constexpr (std::same_as<Output, TokenPosition>) {
                out.push_back({range, source_range});
            } else {
                out.emplace_back(source_range.slice(source));
            }
        }
    }

    // No public token array is built; the current segmenters still compute rune ranges first.
    // Visitors may throw, but must not reuse this decoding buffer or invalidate the input text.
    template <StringLike Input, typename Emit>
        requires std::invocable<Emit &, TokenView<output_char_type_t<Input>>>
                 && std::same_as<std::invoke_result_t<Emit &, TokenView<output_char_type_t<Input>>>, void>
    auto cut_each(const Input &input, CutMode mode, Emit &&emit, UnicodeWithOffset &decoded) const -> void {
        const auto source = as_view(input);
        const auto ranges = prepare_and_cut(source, mode, decoded);
        for (const auto &range : ranges) {
            const auto source_range = to_source_range(range, decoded.offsets);
            std::invoke(emit, TokenView<output_char_type_t<Input>>{source_range.slice(source), {range, source_range}});
        }
    }

    // Move the source into its final owner before building positions, including for small strings.
    template <CharType CharT>
    [[nodiscard]] auto cut_owned(std::basic_string<CharT> source, CutMode mode) const -> OwnedTokens<CharT> {
        auto result = OwnedTokens<CharT>{std::move(source), {}};
        auto decoded = UnicodeWithOffset{};
        cut_into(result.source_, mode, result.positions_, decoded);
        return result;
    }

    // UTF-8 byte buffers produce std::string words; character inputs retain their native string type.
    template <StringLike Input>
    [[nodiscard]] auto cut_strings(const Input &input, CutMode mode) const
        -> std::vector<std::basic_string<output_char_type_t<Input>>> {
        auto decoded = UnicodeWithOffset{};
        auto result = std::vector<std::basic_string<output_char_type_t<Input>>>{};
        cut_into(input, mode, result, decoded);
        return result;
    }

    // Predecoded input retains rune coordinates and never constructs source text or an offset table.
    [[nodiscard]] auto cut_runes(std::span<const Rune> runes, CutMode mode) const -> std::vector<WordRange> {
        detail::check_cut_mode(mode);
        return cut_impl(runes, mode);
    }

    auto cut_runes_into(std::span<const Rune> runes, CutMode mode, std::vector<WordRange> &out) const -> void {
        out.clear();
        const auto ranges = cut_runes(runes, mode);
        out.assign(ranges.begin(), ranges.end());
    }

private:
    template <CharType CharT>
    auto prepare_and_cut(std::basic_string_view<CharT> source, CutMode mode, UnicodeWithOffset &decoded) const
        -> std::vector<WordRange> {
        detail::check_cut_mode(mode);
        decode_with_offset_into(source, decoded);
        return cut_impl(decoded.runes, mode);
    }

    [[nodiscard]] static auto to_source_range(WordRange range, std::span<const uint32_t> offsets) -> SourceRange {
        assert_check([&] { return range.begin <= range.end && range.end < offsets.size(); },
                     "Jieba rune range exceeds its source offset table");
        return {offsets[range.begin], offsets[range.end]};
    }

    [[nodiscard]] auto cut_impl(std::span<const Rune> runes, CutMode mode) const -> std::vector<WordRange> {
        auto result = std::vector<WordRange>{};
        switch (mode) {
            case CutMode::MIX:
                result = MixSegment<true>::cut(dict_, model_, runes);
                break;
            case CutMode::MIX_NO_HMM:
                result = MixSegment<false>::cut(dict_, model_, runes);
                break;
            case CutMode::FULL:
                result = FullSegment::cut(dict_, runes);
                break;
            case CutMode::SEARCH:
                result = QuerySegment<true>::cut(dict_, model_, runes);
                break;
            case CutMode::SEARCH_NO_HMM:
                result = QuerySegment<false>::cut(dict_, model_, runes);
                break;
            case CutMode::HMM:
                result = HMMSegment::cut(model_, runes);
                break;
            case CutMode::MP:
                result = MPSegment::cut(dict_, runes);
                break;
            default:
                assert_check([] { return false; }, "Unchecked Jieba cut mode reached internal dispatch");
                std::unreachable();
        }
        assert_check([&] { return detail::valid_jieba_result(mode, result, runes.size()); },
                     "Jieba: invalid segmentation result for {} runes", runes.size());
        return result;
    }

    DictTrie dict_;
    HMModel model_;
};

} // namespace neo_cppjieba
