#pragma once

#include "DictTrie.hpp"
#include "HMMSegment.hpp"
#include "HMModel.hpp"
#include "MPSegment.hpp"
#include "StringUtil.hpp"
#include "Traits.hpp"
#include "Unicode.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace neo_cppjieba {

/// Stateless mixed-mode segmentation operator.
///
/// Mix mode ("混合模式") combines maximum-probability (MP) segmentation with
/// HMM-based segmentation. It first runs MPSegment to get an initial word
/// sequence, then identifies runs of consecutive single characters that are
/// NOT user-dictionary single-char words, and re-segments those runs using
/// HMMSegment to discover OOV (out-of-vocabulary) words.
///
/// This is the default and recommended segmentation mode in jieba — it offers
/// the best balance between dictionary-based precision and OOV recall.
///
/// This is a purely static utility — no instance state, no ownership of
/// dictionaries or models. The DictTrie and HMModel are taken as const
/// reference parameters.
template <bool hmm = true>
struct MixSegment {
    [[nodiscard]] static auto cut(const DictTrie &dict, const HMModel &model, const Unicode &unicodes)
        -> std::vector<WordRange> {
        auto range = std::vector<WordRange>{};
        range.reserve(unicodes.size() / 2);
        auto segments = get_pre_filter_separators(unicodes);
        auto span = std::span<const Rune>{unicodes.data(), unicodes.size()};
        auto pos = uint32_t{0};
        // first segment.
        cut_one_segment(dict, model, range, span.subspan(pos, segments[0] - pos), pos);
        for (auto i = size_t{0}; i < segments.size() - 1; ++i) {
            // separator segment.
            range.push_back(WordRange{segments[i], segments[i] + 1});
            pos = segments[i] + 1;
            // next text segment.
            cut_one_segment(dict, model, range, span.subspan(pos, segments[i + 1] - pos), pos);
        }
        return range;
    }

    /// Perform mix-mode segmentation on an input string.
    ///
    /// The sentence is first decoded to Unicode, then split by default separators.
    /// Each non-separator segment is segmented using mix-mode, while separator
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
        result.reserve(unicode.size() / 2);

        auto range = cut(dict, model, unicode);
        auto view = as_view(sentence);
        for (auto &r : range) {
            result.push_back(encode(view, offsets, r));
        }

        return result;
    }

private:
    /// Perform mix-mode segmentation on a separator-free Unicode rune sequence.
    static auto cut_one_segment(const DictTrie &dict, const HMModel &model, std::vector<WordRange> &result,
                                std::span<const Rune> runes, uint32_t pos) -> void {
        if (runes.empty()) {
            return;
        }

        auto segment = Unicode{runes.begin(), runes.end()};
        auto mp_words = MPSegment::cut(dict, segment);

        if constexpr (!hmm) {
            for (auto &word : mp_words) {
                result.push_back(WordRange{pos + word.begin, pos + word.end});
            }
            return;
        }

        auto i = size_t{0};
        while (i < mp_words.size()) {
            auto &word = mp_words[i];

            // Multi-character word or user-dict single Chinese character → emit directly.
            if (word.size() > 1 || (word.size() == 1 && dict.is_user_dict_single_chinese_word(runes[word.begin]))) {
                result.push_back(WordRange{pos + word.begin, pos + word.end});
                ++i;
                continue;
            }

            // Collect consecutive single-character words that are not user-dict words.
            auto j = i;
            while (j < mp_words.size() && mp_words[j].size() == 1
                   && !dict.is_user_dict_single_chinese_word(runes[mp_words[j].begin])) {
                ++j;
            }

            auto run_begin = mp_words[i].begin;
            auto run_end = mp_words[j - 1].end;
            auto hmm_segment = Unicode{runes.begin() + run_begin, runes.begin() + run_end};
            auto hmm_words = HMMSegment::cut(model, hmm_segment);
            for (auto &hmm_word : hmm_words) {
                result.push_back(WordRange{pos + run_begin + hmm_word.begin, pos + run_begin + hmm_word.end});
            }

            i = j;
        }
    }
};

} // namespace neo_cppjieba
