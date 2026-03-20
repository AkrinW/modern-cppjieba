#pragma once

#include "DictTrie.hpp"
#include "HMModel.hpp"
#include "MixSegment.hpp"
#include "Traits.hpp"
#include "Unicode.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace neo_cppjieba {

/// Stateless query-mode segmentation operator.
///
/// Query mode ("搜索引擎模式") is designed for search-engine indexing. It first
/// runs MixSegment to obtain an initial word sequence, then for each word longer
/// than 2 characters, it additionally emits all 2-gram and 3-gram sub-words that
/// exist in the dictionary. This produces more fine-grained tokens that improve
/// search recall.
///
///   Example: "中国科学院" → "中国/科学/学院/科学院/中国科学院"
///
/// This is a purely static utility — no instance state, no ownership of
/// dictionaries or models. The DictTrie and HMModel are taken as const
/// reference parameters.
template <bool hmm = true>
struct QuerySegment {
    [[nodiscard]] static auto cut(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes)
        -> std::vector<WordRange> {
        auto mix_words = MixSegment<hmm>::cut(dict, model, runes);

        auto result = std::vector<WordRange>{};
        result.reserve(mix_words.size() * 2);

        for (auto &word : mix_words) {
            auto len = word.size();

            if (len > 2) {
                for (auto i = uint32_t{0}; i + 2 <= len; ++i) {
                    auto sub = runes.subspan(word.begin + i, 2);
                    if (dict.find(sub).has_value()) {
                        result.push_back(WordRange{word.begin + i, word.begin + i + 2});
                    }
                }
            }

            if (len > 3) {
                for (auto i = uint32_t{0}; i + 3 <= len; ++i) {
                    auto sub = runes.subspan(word.begin + i, 3);
                    if (dict.find(sub).has_value()) {
                        result.push_back(WordRange{word.begin + i, word.begin + i + 3});
                    }
                }
            }

            result.push_back(word);
        }

        return result;
    }

    [[nodiscard]] static auto cut(const DictTrie &dict, const HMModel &model, const Unicode &unicodes)
        -> std::vector<WordRange> {
        return cut(dict, model, std::span<const Rune>{unicodes.data(), unicodes.size()});
    }

    /// Perform query-mode segmentation on an input string.
    ///
    /// The sentence is first decoded to Unicode, then split by default separators.
    /// Each non-separator segment is segmented using query-mode, while separator
    /// characters are emitted as individual tokens.
    ///
    /// @param dict     the dictionary trie
    /// @param model    the HMM model
    /// @param sentence the input string
    /// @return         a vector of strings, each representing a segmented word
    template <StringLike T>
    [[nodiscard]] static auto cut(const DictTrie &dict, const HMModel &model, const T &sentence)
        -> std::vector<std::basic_string<resolve_char_type_t<T>>> {
        using CharT = resolve_char_type_t<T>;
        auto unicode_with_offset = decode_with_offset(sentence);
        const auto &unicode = unicode_with_offset.runes;
        const auto &offsets = unicode_with_offset.offsets;
        auto result = std::vector<std::basic_string<CharT>>{};
        result.reserve(unicode.size());

        auto range = cut(dict, model, unicode);
        auto view = as_view(sentence);
        for (auto &r : range) {
            result.push_back(encode(view, offsets, r));
        }

        return result;
    }
};

} // namespace neo_cppjieba
