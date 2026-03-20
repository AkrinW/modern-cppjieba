#pragma once

#include "Dag.hpp"
#include "DictTrie.hpp"
#include "StringUtil.hpp"
#include "Traits.hpp"
#include "Unicode.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace neo_cppjieba {

/// Stateless full-mode segmentation operator.
///
/// Full mode ("全模式") outputs ALL possible dictionary words present in the
/// input sentence. Single characters that are not covered by any dictionary
/// word are also emitted as fallback tokens.
///
/// This is a purely static utility — no instance state, no ownership of
/// dictionaries. The DictTrie is taken as a const reference parameter.
struct FullSegment {
    [[nodiscard]] static auto cut(const DictTrie &dict, std::span<const Rune> runes) -> std::vector<WordRange> {
        auto range = std::vector<WordRange>{};
        range.reserve(runes.size() / 2);
        auto segments = get_pre_filter_separators(runes);
        auto pos = uint32_t{0};
        // first segment.
        cut_one_segment(dict, range, runes.subspan(pos, segments[0] - pos), pos);
        for (auto i = size_t{0}; i < segments.size() - 1; ++i) {
            // separator segment.
            range.push_back(WordRange{segments[i], segments[i] + 1});
            pos = segments[i] + 1;
            // next text segment.
            cut_one_segment(dict, range, runes.subspan(pos, segments[i + 1] - pos), pos);
        }
        return range;
    }

    [[nodiscard]] static auto cut(const DictTrie &dict, const Unicode &unicodes) -> std::vector<WordRange> {
        return cut(dict, std::span<const Rune>{unicodes.data(), unicodes.size()});
    }

    /// Perform full-mode segmentation on an input string.
    ///
    /// The sentence is first decoded to Unicode, then split by default separators.
    /// Each non-separator segment is segmented using the dictionary, while separator
    /// characters are emitted as individual tokens.
    ///
    /// @param dict     the dictionary trie to look up words
    /// @param sentence the input string
    /// @return         a vector of strings, each representing a segmented word
    template <StringLike T>
    [[nodiscard]] static auto cut(const DictTrie &dict, const T &sentence)
        -> std::vector<std::basic_string<resolve_char_type_t<T>>> {
        using CharT = resolve_char_type_t<T>;
        auto unicode_with_offset = decode_with_offset(sentence);
        const auto &unicode = unicode_with_offset.runes;
        const auto &offsets = unicode_with_offset.offsets;
        auto result = std::vector<std::basic_string<CharT>>{};
        result.reserve(unicode.size() / 2);

        auto range = cut(dict, unicode);
        auto view = as_view(sentence);
        for (auto &r : range) {
            result.push_back(encode(view, offsets, r));
        }

        return result;
    }

private:
    static auto cut_one_segment(const DictTrie &dict, std::vector<WordRange> &result, std::span<const Rune> runes,
                                uint32_t pos) -> void {
        if (runes.empty()) {
            return;
        }

        auto dag = dict.find_dag(runes);
        auto n = dag.size();

        // max_covered tracks the furthest rune position covered by previously emitted dictionary words; used to decide
        // whether to emit single-char fallback tokens.
        auto max_covered = size_t{0};

        for (auto i = size_t{0}; i < n; ++i) {
            auto edges = dag.get_edges(i);
            auto edge_count = edges.size();

            if (edge_count == 1) {
                // only one edge, must be a single character (either dict single-char or fallback non-dict)
                // emit as fallback if the position is not already covered by previous longer words
                if (max_covered <= i) {
                    result.push_back(WordRange{pos + static_cast<uint32_t>(i), pos + edges[0].next_pos});
                    max_covered = static_cast<size_t>(edges[0].next_pos);
                }
            } else {
                // multiple edges, meaning there are longer words starting at this position
                // emit all of them (including single-char if it exists) since full mode includes all overlapping
                // matches
                for (auto j = size_t{1}; j < edge_count; ++j) {
                    result.push_back(WordRange{pos + static_cast<uint32_t>(i), pos + edges[j].next_pos});
                }

                // since edges are sorted from shortest to longest, the last edge extends the furthest; update
                // max_covered accordingly
                max_covered = std::max(max_covered, static_cast<size_t>(edges.back().next_pos));
            }
        }
    }
};

} // namespace neo_cppjieba
