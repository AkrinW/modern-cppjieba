#pragma once

#include "neo/DictTrie.hpp"
#include "neo/HMMSegment.hpp"
#include "neo/HMModel.hpp"
#include "neo/MPSegment.hpp"
#include "neo/MixSegment.hpp"
#include "neo/StringUtil.hpp"
#include "neo/Unicode.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace neo_cppjieba::test {

namespace detail {

template <bool hmm>
inline auto append_sub_words_by_lookup(const DictTrie &dict, std::span<const Rune> runes, WordRange word,
                                       std::vector<WordRange> &result) -> void {
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

inline auto has_dag_edge(const Dag &dag, uint32_t begin, uint32_t end) -> bool {
    for (auto &&edge : dag.get_edges(begin)) {
        if (edge.next_pos == end) {
            return true;
        }
    }
    return false;
}

inline auto append_sub_words_from_dag(const Dag &dag, uint32_t segment_offset, WordRange word,
                                      std::vector<WordRange> &result) -> void {
    auto len = word.size();
    auto local_begin = word.begin - segment_offset;

    if (len > 2) {
        for (auto i = uint32_t{0}; i + 2 <= len; ++i) {
            auto begin = local_begin + i;
            auto end = begin + 2;
            if (has_dag_edge(dag, begin, end)) {
                result.push_back(WordRange{word.begin + i, word.begin + i + 2});
            }
        }
    }

    if (len > 3) {
        for (auto i = uint32_t{0}; i + 3 <= len; ++i) {
            auto begin = local_begin + i;
            auto end = begin + 3;
            if (has_dag_edge(dag, begin, end)) {
                result.push_back(WordRange{word.begin + i, word.begin + i + 3});
            }
        }
    }

    result.push_back(word);
}

template <bool hmm>
inline auto append_mix_words(const DictTrie &dict, const HMModel &model, std::vector<WordRange> &result,
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
        if (word.size() > 1 || (word.size() == 1 && dict.is_user_dict_single_chinese_word(runes[word.begin]))) {
            result.push_back(WordRange{pos + word.begin, pos + word.end});
            ++i;
            continue;
        }

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

template <bool hmm>
struct BufferedMixResult {
    struct SegmentDag {
        uint32_t offset{0};
        Dag dag;

        [[nodiscard]] auto end() const noexcept -> uint32_t {
            return offset + static_cast<uint32_t>(dag.size());
        }
    };

    std::vector<WordRange> words;
    std::vector<SegmentDag> dags;
};

template <bool hmm>
inline auto mix_cut_with_dag_buffered(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes)
    -> BufferedMixResult<hmm> {
    auto result = BufferedMixResult<hmm>{};
    result.words.reserve(runes.size() / 2);

    auto segments = get_pre_filter_separators(runes);
    auto pos = uint32_t{0};

    auto cut_one = [&](std::span<const Rune> segment_runes, uint32_t segment_pos) {
        if (segment_runes.empty()) {
            return;
        }
        auto mp_result = MPSegment::cut_segment(dict, segment_runes);
        result.dags.push_back(typename BufferedMixResult<hmm>::SegmentDag{segment_pos, std::move(mp_result.dag)});
        append_mix_words<hmm>(dict, model, result.words, mp_result.words, segment_runes, segment_pos);
    };

    cut_one(runes.subspan(pos, segments[0] - pos), pos);
    for (auto i = size_t{0}; i < segments.size() - 1; ++i) {
        result.words.push_back(WordRange{segments[i], segments[i] + 1});
        pos = segments[i] + 1;
        cut_one(runes.subspan(pos, segments[i + 1] - pos), pos);
    }

    return result;
}

template <bool hmm>
inline auto query_cut_with_requery(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes)
    -> std::vector<WordRange> {
    auto mix_words = MixSegment<hmm>::cut(dict, model, runes);
    auto result = std::vector<WordRange>{};
    result.reserve(mix_words.size() * 2);

    for (auto &word : mix_words) {
        append_sub_words_by_lookup<hmm>(dict, runes, word, result);
    }

    return result;
}

template <bool hmm>
inline auto query_cut_with_buffered_dag(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes)
    -> std::vector<WordRange> {
    auto mix_result = mix_cut_with_dag_buffered<hmm>(dict, model, runes);
    auto result = std::vector<WordRange>{};
    result.reserve(mix_result.words.size() * 2);

    auto dag_index = size_t{0};
    for (auto &word : mix_result.words) {
        while (dag_index < mix_result.dags.size() && mix_result.dags[dag_index].end() <= word.begin) {
            ++dag_index;
        }

        const auto *segment = static_cast<const typename BufferedMixResult<hmm>::SegmentDag *>(nullptr);
        if (dag_index < mix_result.dags.size()) {
            auto &candidate = mix_result.dags[dag_index];
            if (candidate.offset <= word.begin && word.end <= candidate.end()) {
                segment = &candidate;
            }
        }

        if (segment) {
            append_sub_words_from_dag(segment->dag, segment->offset, word, result);
        } else {
            append_sub_words_by_lookup<hmm>(dict, runes, word, result);
        }
    }

    return result;
}

} // namespace detail

template <bool hmm = true>
inline auto query_cut_requery(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes)
    -> std::vector<WordRange> {
    return detail::query_cut_with_requery<hmm>(dict, model, runes);
}

template <bool hmm = true>
inline auto query_cut_buffered_dag(const DictTrie &dict, const HMModel &model, std::span<const Rune> runes)
    -> std::vector<WordRange> {
    return detail::query_cut_with_buffered_dag<hmm>(dict, model, runes);
}

} // namespace neo_cppjieba::test
