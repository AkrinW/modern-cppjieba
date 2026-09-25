#pragma once

#include "neo/Token.hpp"
#include "neo/Traits.hpp"
#include "neo/Unicode.hpp"
#include "neo/Workspace.hpp"
#include "neo/detail/DictTrie.hpp"
#include "neo/detail/FullSegment.hpp"
#include "neo/detail/HMMSegment.hpp"
#include "neo/detail/HMModel.hpp"
#include "neo/detail/Logging.hpp"
#include "neo/detail/MPSegment.hpp"
#include "neo/detail/MixSegment.hpp"
#include "neo/detail/Output.hpp"
#include "neo/detail/QuerySegment.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
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
        auto workspace = Workspace{};
        cut_into(source, mode, positions, workspace);
        return Tokens<output_char_type_t<Input>>{source, std::move(positions)};
    }

    // Replaces output while retaining its capacity. Output records contain no decoding-buffer pointers.
    // Caller-owned decoding buffers can be reused across inputs; results never borrow this storage.
    // Input must not alias the output array or either decoding buffer.
    // The workspace also retains DAG, DP, HMM, separator, and rune-result storage.
    template <StringLike Input, typename Output>
        requires detail::OutputValue<Output, output_char_type_t<Input>>
    auto cut_into(const Input &input, CutMode mode, std::vector<Output> &out, Workspace &workspace) const -> void {
        out.clear();
        const auto source = as_view(input);
        const auto ranges = prepare_and_cut(source, mode, workspace);
        detail::fill_output(source, ranges, workspace.decoded_.offsets, out);
    }

    // No public token array is built; the current segmenters still compute rune ranges first.
    // Visitors may throw, but must not reuse this decoding buffer or invalidate the input text.
    template <StringLike Input, typename Emit>
        requires detail::TokenVisitor<Emit, output_char_type_t<Input>>
    auto cut_each(const Input &input, CutMode mode, Emit &&emit, Workspace &workspace) const -> void {
        const auto source = as_view(input);
        const auto ranges = prepare_and_cut(source, mode, workspace);
        detail::emit_tokens(source, ranges, workspace.decoded_.offsets, emit);
    }

    // Move the source into its final owner before building positions, including for small strings.
    template <CharType CharT>
    [[nodiscard]] auto cut_owned(std::basic_string<CharT> source, CutMode mode) const -> OwnedTokens<CharT> {
        auto result = OwnedTokens<CharT>{std::move(source), {}};
        auto workspace = Workspace{};
        cut_into(result.source_, mode, result.positions_, workspace);
        return result;
    }

    // UTF-8 byte buffers produce std::string words; character inputs retain their native string type.
    template <StringLike Input>
    [[nodiscard]] auto cut_strings(const Input &input, CutMode mode) const
        -> std::vector<std::basic_string<output_char_type_t<Input>>> {
        auto workspace = Workspace{};
        auto result = std::vector<std::basic_string<output_char_type_t<Input>>>{};
        cut_into(input, mode, result, workspace);
        return result;
    }

    // Predecoded input retains rune coordinates and never constructs source text or an offset table.
    [[nodiscard]] auto cut_runes(std::span<const Rune> runes, CutMode mode) const -> std::vector<WordRange> {
        auto result = std::vector<WordRange>{};
        auto workspace = Workspace{};
        cut_runes_into(runes, mode, result, workspace);
        return result;
    }

    // Fill the caller's rune output directly; decoding storage is unused for predecoded input.
    auto cut_runes_into(std::span<const Rune> runes, CutMode mode, std::vector<WordRange> &out,
                        Workspace &workspace) const -> void {
        out.clear();
        detail::check_cut_mode(mode);
        // Former policy: Preserve the previous empty-output guarantee when segmentation fails.
        // Failures now propagate directly and may leave partial output.
        cut_impl(runes, mode, out, workspace.scratch_);
    }

private:
    template <CharType CharT>
    auto prepare_and_cut(std::basic_string_view<CharT> source, CutMode mode, Workspace &workspace) const
        -> std::span<const WordRange> {
        detail::check_cut_mode(mode);
        decode_with_offset_into(source, workspace.decoded_);
        cut_impl(workspace.decoded_.runes, mode, workspace.ranges_, workspace.scratch_);
        return workspace.ranges_;
    }

    auto cut_impl(std::span<const Rune> runes, CutMode mode, std::vector<WordRange> &result,
                  detail::SegmentScratch &scratch) const -> void {
        switch (mode) {
            case CutMode::MIX:
                MixSegment<true>::cut_into(dict_, model_, runes, result, scratch);
                break;
            case CutMode::MIX_NO_HMM:
                MixSegment<false>::cut_into(dict_, model_, runes, result, scratch);
                break;
            case CutMode::FULL:
                FullSegment::cut_into(dict_, runes, result, scratch);
                break;
            case CutMode::SEARCH:
                QuerySegment<true>::cut_into(dict_, model_, runes, result, scratch);
                break;
            case CutMode::SEARCH_NO_HMM:
                QuerySegment<false>::cut_into(dict_, model_, runes, result, scratch);
                break;
            case CutMode::HMM:
                HMMSegment::cut_into(model_, runes, result, scratch);
                break;
            case CutMode::MP:
                MPSegment::cut_into(dict_, runes, result, scratch);
                break;
            default:
                assert_check([] { return false; }, "Unchecked Jieba cut mode reached internal dispatch");
                std::unreachable();
        }
        assert_check([&] { return detail::valid_jieba_result(mode, result, runes.size()); },
                     "Jieba: invalid segmentation result for {} runes", runes.size());
    }

    DictTrie dict_;
    HMModel model_;
};

} // namespace neo_cppjieba
