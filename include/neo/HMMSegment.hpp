#pragma once

#include "HMModel.hpp"
#include "StringUtil.hpp"
#include "Traits.hpp"
#include "Unicode.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace neo_cppjieba {

/// Stateless HMM-based segmentation operator.
///
/// HMM mode uses a Hidden Markov Model with the Viterbi algorithm to segment
/// Chinese text that is not covered by the dictionary. It assigns each character
/// one of four hidden states — B (Begin), E (End), M (Middle), S (Single) —
/// and uses word boundaries indicated by E/S states to split the input.
///
/// ASCII sequences (letters and numbers) are handled by dedicated rules that
/// group them into single tokens before HMM processing.
///
/// This is a purely static utility — no instance state, no ownership of the
/// model. The HMModel is taken as a const reference parameter.
struct HMMSegment {
    [[nodiscard]] static auto cut(const HMModel &model, std::span<const Rune> runes) -> std::vector<WordRange> {
        auto range = std::vector<WordRange>{};
        range.reserve(runes.size() / 2);
        auto segments = get_pre_filter_separators(runes);
        auto pos = uint32_t{0};
        // first segment.
        cut_one_segment(model, range, runes.subspan(pos, segments[0] - pos), pos);
        for (auto i = size_t{0}; i < segments.size() - 1; ++i) {
            // separator segment.
            range.push_back(WordRange{segments[i], segments[i] + 1});
            pos = segments[i] + 1;
            // next text segment.
            cut_one_segment(model, range, runes.subspan(pos, segments[i + 1] - pos), pos);
        }
        return range;
    }

    [[nodiscard]] static auto cut(const HMModel &model, const Unicode &unicodes) -> std::vector<WordRange> {
        return cut(model, std::span<const Rune>{unicodes.data(), unicodes.size()});
    }

    /// Perform HMM segmentation on a UTF-8 string, with pre-filtering.
    ///
    /// The sentence is first decoded to Unicode, then split by default separators.
    /// Each non-separator segment is segmented using the HMM, while separator
    /// characters are emitted as individual tokens.
    ///
    /// @param model    the HMM model
    /// @param sentence the input UTF-8 string
    /// @return         a vector of UTF-8 strings, each representing a segmented word
    template <StringLike T>
    [[nodiscard]] static auto cut(const HMModel &model, const T &sentence)
        -> std::vector<std::basic_string<resolve_char_type_t<T>>> {
        using CharT = resolve_char_type_t<T>;
        auto unicode_with_offset = decode_with_offset(sentence);
        const auto &unicode = unicode_with_offset.runes;
        const auto &offsets = unicode_with_offset.offsets;
        auto result = std::vector<std::basic_string<CharT>>{};
        result.reserve(unicode.size() / 2);

        auto range = cut(model, unicode);
        auto view = as_view(sentence);
        for (auto &r : range) {
            result.push_back(encode(view, offsets, r));
        }

        return result;
    }

private:
    /// Perform HMM segmentation on a Unicode rune sequence.
    ///
    /// @param model  the HMM model (start/trans/emit probabilities)
    /// @param runes  the input sentence, already decoded to Unicode code points
    /// @return       a vector of WordRange, each representing a segmented word
    static auto cut_one_segment(const HMModel &model, std::vector<WordRange> &range, std::span<const Rune> runes, uint32_t pos)
        -> void {
        if (runes.empty()) {
            return;
        }

        auto n = static_cast<uint32_t>(runes.size());

        auto left = uint32_t{0};
        auto right = uint32_t{0};

        while (right < n) {
            if (runes[right] < 0x80) {
                // Flush pending Chinese characters to HMM
                if (left < right) {
                    internal_cut(model, runes, left, right, range, pos);
                }
                left = right;

                // Try ASCII grouping rules
                auto end = sequential_letter_end(runes, left, n);
                if (end == left) {
                    end = number_end(runes, left, n);
                }
                if (end == left) {
                    end = left + 1;
                }
                range.push_back(WordRange{pos + left, pos + end});
                right = end;
                left = right;
            } else {
                ++right;
            }
        }

        // Flush remaining Chinese characters
        if (left < right) {
            internal_cut(model, runes, left, right, range, pos);
        }
    }

    /// Find the end position of a consecutive ASCII letter sequence starting at `begin`.
    /// Returns `begin` if runes[begin] is not a letter.
    [[nodiscard]] static constexpr auto sequential_letter_end(std::span<const Rune> runes, uint32_t begin,
                                                              uint32_t end) noexcept -> uint32_t {
        auto r = runes[begin];
        if (('a' > r || r > 'z') && ('A' > r || r > 'Z')) {
            return begin;
        }
        auto pos = begin + 1;
        while (pos < end) {
            r = runes[pos];
            if (('a' <= r && r <= 'z') || ('A' <= r && r <= 'Z') || ('0' <= r && r <= '9')) {
                ++pos;
            } else {
                break;
            }
        }
        return pos;
    }

    /// Find the end position of a consecutive number sequence starting at `begin`.
    /// Returns `begin` if runes[begin] is not a digit.
    [[nodiscard]] static constexpr auto number_end(std::span<const Rune> runes, uint32_t begin, uint32_t end) noexcept
        -> uint32_t {
        auto r = runes[begin];
        if ('0' > r || r > '9') {
            return begin;
        }
        auto pos = begin + 1;
        while (pos < end) {
            r = runes[pos];
            if (('0' <= r && r <= '9') || r == '.') {
                ++pos;
            } else {
                break;
            }
        }
        return pos;
    }

    /// Run the Viterbi algorithm on runes[begin..end) and append segmented WordRanges to result.
    static auto internal_cut(const HMModel &model, std::span<const Rune> runes, uint32_t begin, uint32_t end,
                             std::vector<WordRange> &result, uint32_t pos) -> void {
        constexpr auto Y = kHMMStatesNum; // 4 states: B, E, M, S
        auto X = static_cast<size_t>(end - begin);

        // Flat 2D arrays laid out as [state * X + position] for cache-friendly access
        auto weight = std::vector<double>(X * Y);
        auto path = std::vector<uint8_t>(X * Y);

        // ── Initialization (t = 0) ──────────────────────────────────────
        for (auto y = size_t{0}; y < Y; ++y) {
            weight[y * X] = model.get_start_prob(static_cast<HMMState>(y))
                            + model.get_emit_prob(static_cast<HMMState>(y), runes[begin]);
            path[y * X] = 0;
        }

        // ── Recursion (t = 1 .. X-1) ────────────────────────────────────
        for (auto x = size_t{1}; x < X; ++x) {
            auto rune = runes[begin + x];
            for (auto y = size_t{0}; y < Y; ++y) {
                auto emit = model.get_emit_prob(static_cast<HMMState>(y), rune);
                auto best_weight = MIN_DOUBLE;
                // Default to E (End) state — matches the original cppjieba behavior.
                // When all transition weights tie at MIN_DOUBLE (e.g., for characters absent
                // from emit_prob_map), this default determines the backtrace path.
                auto best_prev = static_cast<uint8_t>(HMMState::E);

                for (auto prev_y = size_t{0}; prev_y < Y; ++prev_y) {
                    auto w = weight[prev_y * X + (x - 1)]
                             + model.get_trans_prob(static_cast<HMMState>(prev_y), static_cast<HMMState>(y)) + emit;
                    if (w > best_weight) {
                        best_weight = w;
                        best_prev = static_cast<uint8_t>(prev_y);
                    }
                }

                weight[y * X + x] = best_weight;
                path[y * X + x] = best_prev;
            }
        }

        // ── Termination: pick the best final state (must be E or S) ─────
        auto last = X - 1;
        auto end_weight = weight[static_cast<size_t>(HMMState::E) * X + last];
        auto end_state = uint8_t{static_cast<uint8_t>(HMMState::E)};
        auto s_weight = weight[static_cast<size_t>(HMMState::S) * X + last];
        if (s_weight > end_weight) {
            end_state = static_cast<uint8_t>(HMMState::S);
        }

        // ── Backtrace ───────────────────────────────────────────────────
        auto status = std::vector<uint8_t>(X);
        status[last] = end_state;
        for (auto x = static_cast<int64_t>(last) - 1; x >= 0; --x) {
            status[static_cast<size_t>(x)] = path[status[static_cast<size_t>(x + 1)] * X + static_cast<size_t>(x + 1)];
        }

        // ── Emit word ranges based on E/S boundaries ────────────────────
        auto word_begin = begin;
        for (auto i = size_t{0}; i < X; ++i) {
            auto state = static_cast<HMMState>(status[i]);
            if (state == HMMState::E || state == HMMState::S) {
                result.push_back(WordRange{pos + word_begin, pos + begin + static_cast<uint32_t>(i) + 1});
                word_begin = begin + static_cast<uint32_t>(i) + 1;
            }
        }
    }
};

} // namespace neo_cppjieba
