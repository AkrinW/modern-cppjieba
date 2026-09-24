#pragma once

#include "DictTrie.hpp"
#include "FullSegment.hpp"
#include "HMMSegment.hpp"
#include "HMModel.hpp"
#include "Logging.hpp"
#include "MPSegment.hpp"
#include "MixSegment.hpp"
#include "QuerySegment.hpp"
#include "Traits.hpp"
#include "Unicode.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace neo_cppjieba {

enum class CutMethod : uint8_t { MIX, FULL, SEARCH, HMM, MP };

namespace detail {

// Public entry points accept only named segmentation modes.
template <CutMethod M>
concept ValidCutMethod =
    M == CutMethod::MIX || M == CutMethod::FULL || M == CutMethod::SEARCH || M == CutMethod::HMM || M == CutMethod::MP;

// Partition modes cover every rune once; full and search modes may emit overlapping words.
template <CutMethod M>
[[nodiscard]] inline auto valid_jieba_result(std::span<const WordRange> words, size_t rune_count) -> bool {
    if constexpr (M == CutMethod::MIX || M == CutMethod::HMM || M == CutMethod::MP) {
        return valid_segment_partition(words, rune_count);
    } else {
        if (rune_count == 0) {
            return words.empty();
        }
        return !words.empty() && std::ranges::all_of(words, [rune_count](const WordRange &word) {
            return word.begin < word.end && word.end <= rune_count;
        });
    }
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
class Jieba {
public:
    // An empty user dictionary path explicitly disables user dictionary loading.
    explicit Jieba(std::string_view dict_path, std::string_view model_path, std::string_view user_dict_path)
        : dict_(dict_path, user_dict_path), model_(model_path) {
    }

    template <StringLike Input>
    [[nodiscard]] static auto decode(const Input &input) -> Unicode {
        return neo_cppjieba::decode(input);
    }

    template <StringLike Input>
    [[nodiscard]] static auto decode_with_offset(const Input &input) -> UnicodeWithOffset {
        return neo_cppjieba::decode_with_offset(input);
    }

    template <CharType Output = char>
    [[nodiscard]] static auto encode(std::span<const Rune> runes, WordRange range) -> std::basic_string<Output> {
        return neo_cppjieba::encode<Output>(range.slice(runes));
    }

    template <CharType CharT>
    [[nodiscard]] static auto encode(std::basic_string_view<CharT> source, std::span<const uint32_t> offsets,
                                     WordRange range) -> std::basic_string<CharT> {
        return neo_cppjieba::encode(source, offsets, range);
    }

    template <CharType Output = char>
    [[nodiscard]] static auto encode_words(std::span<const Rune> runes, std::span<const WordRange> ranges)
        -> std::vector<std::basic_string<Output>> {
        auto result = std::vector<std::basic_string<Output>>{};
        result.reserve(ranges.size());
        for (const auto &range : ranges) {
            result.push_back(encode<Output>(runes, range));
        }
        return result;
    }

    template <CharType CharT>
    [[nodiscard]] static auto encode_words(std::basic_string_view<CharT> source, std::span<const uint32_t> offsets,
                                           std::span<const WordRange> ranges) -> std::vector<std::basic_string<CharT>> {
        auto result = std::vector<std::basic_string<CharT>>{};
        result.reserve(ranges.size());
        for (const auto &range : ranges) {
            result.push_back(encode(source, offsets, range));
        }
        return result;
    }

    template <CutMethod M = CutMethod::MIX, bool hmm = true, typename RuneT, size_t Extent>
        requires(detail::ValidCutMethod<M> && (std::same_as<RuneT, Rune> || std::same_as<RuneT, const Rune>))
    [[nodiscard]] auto cut(std::span<RuneT, Extent> runes) const -> std::vector<WordRange> {
        auto result = cut_impl<M, hmm>(runes);
        assert_check([&] { return detail::valid_jieba_result<M>(result, runes.size()); },
                     "Jieba: invalid segmentation result for {} runes", runes.size());
        return result;
    }

    template <CutMethod M = CutMethod::MIX, bool hmm = true>
        requires detail::ValidCutMethod<M>
    [[nodiscard]] auto cut(const Unicode &unicodes) const -> std::vector<WordRange> {
        return cut<M, hmm>(std::span<const Rune>{unicodes.data(), unicodes.size()});
    }

    template <CutMethod M = CutMethod::MIX, bool hmm = true>
        requires detail::ValidCutMethod<M>
    [[nodiscard]] auto cut(const UnicodeWithOffset &decoded) const -> std::vector<WordRange> {
        return cut<M, hmm>(std::span<const Rune>{decoded.runes.data(), decoded.runes.size()});
    }

    // UTF-8 byte buffers produce std::string words; character inputs retain their native string type.
    template <CutMethod M = CutMethod::MIX, bool hmm = true, StringLike Input>
        requires detail::ValidCutMethod<M>
    [[nodiscard]] auto cut(const Input &input) const -> std::vector<std::basic_string<output_char_type_t<Input>>> {
        const auto source = as_view(input);
        const auto decoded = neo_cppjieba::decode_with_offset(source);
        const auto ranges = cut<M, hmm>(std::span<const Rune>{decoded.runes.data(), decoded.runes.size()});
        auto result = std::vector<std::basic_string<output_char_type_t<Input>>>{};
        result.reserve(ranges.size());
        for (const auto &range : ranges) {
            result.push_back(detail::encode_validated_source(source, decoded.offsets, range));
        }
        return result;
    }

    [[nodiscard]] auto dict() const & noexcept -> const DictTrie & {
        return dict_;
    }
    auto dict() const && noexcept -> const DictTrie & = delete;

    [[nodiscard]] auto model() const & noexcept -> const HMModel & {
        return model_;
    }
    auto model() const && noexcept -> const HMModel & = delete;

private:
    template <CutMethod M, bool hmm>
    [[nodiscard]] auto cut_impl(std::span<const Rune> runes) const -> std::vector<WordRange> {
        static_assert(detail::ValidCutMethod<M>, "Unsupported Jieba cut method");
        switch (M) {
            case CutMethod::MIX:
                return MixSegment<hmm>::cut(dict_, model_, runes);
            case CutMethod::FULL:
                return FullSegment::cut(dict_, runes);
            case CutMethod::SEARCH:
                return QuerySegment<hmm>::cut(dict_, model_, runes);
            case CutMethod::HMM:
                return HMMSegment::cut(model_, runes);
            case CutMethod::MP:
                return MPSegment::cut(dict_, runes);
        }
        std::unreachable();
    }

    DictTrie dict_;
    HMModel model_;
};

} // namespace neo_cppjieba
