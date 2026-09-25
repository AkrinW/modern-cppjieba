#pragma once

#include "neo/Unicode.hpp"
#include "neo/detail/DictTrie.hpp"
#include "neo/detail/HMMSegment.hpp"
#include "neo/detail/HMModel.hpp"
#include "neo/detail/Logging.hpp"
#include "neo/detail/MPSegment.hpp"
#include "neo/detail/SegmentScratch.hpp"
#include "neo/detail/StringUtil.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
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
        auto scratch = detail::SegmentScratch{};
        cut_into(dict, model, runes, result, scratch);
        return result;
    }

    // Replace output while keeping MP and HMM storage alive for later segments and calls.
    static auto cut_into(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes,
                         std::vector<WordRange> &result, detail::SegmentScratch &scratch) -> void {
        assert_check([&] { return runes.size() <= std::numeric_limits<uint32_t>::max(); },
                     "MixSegment: input exceeds the word-range limit");
        result.clear();
        result.reserve(runes.size() / 2);
        cut(dict, model, runes, result, 0, scratch);
    }

private:
    /// Append mix-mode segmentation results while preserving separator runes as standalone tokens.
    static auto cut(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes,
                    std::vector<WordRange> &result, uint32_t pos, detail::SegmentScratch &scratch) -> void {
        assert_check([&] { return runes.size() <= std::numeric_limits<uint32_t>::max() - pos; },
                     "MixSegment: global word offsets overflow");
        get_pre_filter_separators(runes, scratch.separators);
        const auto &segments = scratch.separators;
        auto segment_pos = pos;
        // First text segment before the first separator.
        cut_one_segment(dict, model, result, runes.subspan(0, segments[0]), segment_pos, scratch);
        for (auto i = size_t{0}; i < segments.size() - 1; ++i) {
            // Emit the separator rune itself.
            result.push_back(WordRange{pos + segments[i], pos + segments[i] + 1});
            auto next_begin = segments[i] + 1;
            segment_pos = pos + next_begin;
            // Continue with the following text segment.
            cut_one_segment(dict, model, result, runes.subspan(next_begin, segments[i + 1] - next_begin), segment_pos,
                            scratch);
        }
    }

    /// Perform mix-mode segmentation on a separator-free Unicode rune sequence.
    static auto cut_one_segment(const DictTrie &dict, const HMModel &model, std::vector<WordRange> &result,
                                std::span<const Rune> runes, uint32_t pos, detail::SegmentScratch &scratch) -> void {
        assert_check([&] { return runes.size() <= std::numeric_limits<uint32_t>::max() - pos; },
                     "MixSegment: global word offsets overflow");
        if (runes.empty()) {
            return;
        }

        detail::mp_cut_segment(dict, runes, scratch);
        append_mix_words(dict, model, result, scratch.mp_words, runes, pos, scratch);
    }

    /// Re-segment MP single-character runs with HMM so OOV multi-character words can be recovered.
    static auto append_mix_words(const DictTrie &dict, const HMModel &model, std::vector<WordRange> &result,
                                 const std::vector<WordRange> &mp_words, std::span<const Rune> runes, uint32_t pos,
                                 detail::SegmentScratch &scratch) -> void {
        assert_check([&] { return runes.size() <= std::numeric_limits<uint32_t>::max() - pos; },
                     "MixSegment: global word offsets overflow");
        assert_check([&] { return detail::valid_segment_partition(mp_words, runes.size()); },
                     "MixSegment: MP words must cover the rune span exactly once");
        if constexpr (!hmm) {
            for (const auto &word : mp_words) {
                result.push_back(WordRange{pos + word.begin, pos + word.end});
            }
            return;
        }

        auto i = size_t{0};
        while (i < mp_words.size()) {
            const auto &word = mp_words[i];

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

            assert_check([&] { return i < j; }, "MixSegment: HMM input must contain at least one MP word");
            auto run_begin = mp_words[i].begin;
            auto run_end = mp_words[j - 1].end;
            // The outer cut already removed separators; retain the HMM helper's ASCII handling and rune offsets.
            detail::hmm_cut_one_segment(model, result, runes.subspan(run_begin, run_end - run_begin), pos + run_begin,
                                        scratch);

            i = j;
        }
    }
};

} // namespace neo_cppjieba
