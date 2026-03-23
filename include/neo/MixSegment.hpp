#pragma once

#include "DictTrie.hpp"
#include "HMMSegment.hpp"
#include "HMModel.hpp"
#include "MPSegment.hpp"
#include "StringUtil.hpp"
#include "Unicode.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
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
    [[nodiscard]] static auto cut(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes)
        -> std::vector<WordRange> {
        auto result = std::vector<WordRange>{};
        result.reserve(runes.size() / 2);
        auto segments = get_pre_filter_separators(runes);
        auto pos = uint32_t{0};
        // first segment.
        cut_one_segment(dict, model, result, runes.subspan(pos, segments[0] - pos), pos);
        for (auto i = size_t{0}; i < segments.size() - 1; ++i) {
            // separator segment.
            result.push_back(WordRange{segments[i], segments[i] + 1});
            pos = segments[i] + 1;
            // next text segment.
            cut_one_segment(dict, model, result, runes.subspan(pos, segments[i + 1] - pos), pos);
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

        auto mp_words = MPSegment::cut(dict, runes);
        append_mix_words(dict, model, result, mp_words, runes, pos);
    }

    static auto append_mix_words(const DictTrie &dict, const HMModel &model, std::vector<WordRange> &result,
                                 const std::vector<WordRange> &mp_words, std::span<const Rune> runes, uint32_t pos)
        -> void {
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
            auto hmm_words = HMMSegment::cut(model, runes.subspan(run_begin, run_end - run_begin));
            for (auto &hmm_word : hmm_words) {
                result.push_back(WordRange{pos + run_begin + hmm_word.begin, pos + run_begin + hmm_word.end});
            }

            i = j;
        }
    }
};

} // namespace neo_cppjieba
