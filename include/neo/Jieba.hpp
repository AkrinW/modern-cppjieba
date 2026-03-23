#pragma once

#include "DictTrie.hpp"
#include "FullSegment.hpp"
#include "HMMSegment.hpp"
#include "HMModel.hpp"
#include "MPSegment.hpp"
#include "MixSegment.hpp"
#include "QuerySegment.hpp"
#include "Traits.hpp"
#include "Unicode.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace neo_cppjieba {

enum class CutMethod : uint8_t { MIX, FULL, SEARCH, HMM, MP };

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
    explicit Jieba(std::string_view dict_path, std::string_view model_path, std::string_view user_dict_path = "")
        : dict_(dict_path, user_dict_path), model_(model_path) {
    }

    Jieba() : dict_(resolve("jieba.dict.utf8")), model_(resolve("hmm_model.utf8")) {
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

    template <CutMethod M = CutMethod::MIX, bool hmm = true>
    [[nodiscard]] auto cut(std::span<const Rune> runes) const -> std::vector<WordRange> {
        return cut_impl<M, hmm>(runes);
    }

    template <CutMethod M = CutMethod::MIX, bool hmm = true>
    [[nodiscard]] auto cut(const Unicode &unicodes) const -> std::vector<WordRange> {
        return cut<M, hmm>(std::span<const Rune>{unicodes.data(), unicodes.size()});
    }

    template <CutMethod M = CutMethod::MIX, bool hmm = true>
    [[nodiscard]] auto cut(const UnicodeWithOffset &decoded) const -> std::vector<WordRange> {
        return cut<M, hmm>(std::span<const Rune>{decoded.runes.data(), decoded.runes.size()});
    }

    template <CutMethod M = CutMethod::MIX, bool hmm = true, StringLike Input>
    [[nodiscard]] auto cut(const Input &input) const -> std::vector<std::basic_string<resolve_char_type_t<Input>>> {
        auto decoded = neo_cppjieba::decode_with_offset(input);
        auto ranges = cut<M, hmm>(std::span<const Rune>{decoded.runes.data(), decoded.runes.size()});
        return encode_words(as_view(input), decoded.offsets, ranges);
    }

    [[nodiscard]] auto dict() const noexcept -> const DictTrie & {
        return dict_;
    }

    [[nodiscard]] auto model() const noexcept -> const HMModel & {
        return model_;
    }

private:
    template <CutMethod M, bool hmm>
    [[nodiscard]] auto cut_impl(std::span<const Rune> runes) const -> std::vector<WordRange> {
        if constexpr (M == CutMethod::MIX) {
            return MixSegment<hmm>::cut(dict_, model_, runes);
        } else if constexpr (M == CutMethod::FULL) {
            return FullSegment::cut(dict_, runes);
        } else if constexpr (M == CutMethod::SEARCH) {
            return QuerySegment<hmm>::cut(dict_, model_, runes);
        } else if constexpr (M == CutMethod::HMM) {
            return HMMSegment::cut(model_, runes);
        } else {
            return MPSegment::cut(dict_, runes);
        }
    }

    [[nodiscard]] static auto resolve(std::string_view filename) -> std::string {
        auto path = std::string{__FILE__};
        for (auto i = 0; i < 2; ++i) {
            auto pos = path.find_last_of("/\\");
            if (pos != std::string::npos) {
                path.resize(pos);
            }
        }
        path += "/dict/";
        path += filename;
        return path;
    }

    DictTrie dict_;
    HMModel model_;
};

} // namespace neo_cppjieba
