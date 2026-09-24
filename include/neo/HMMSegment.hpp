#pragma once

#include "HMModel.hpp"
#include "Logging.hpp"
#include "StringUtil.hpp"
#include "Unicode.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace neo_cppjieba {

namespace detail {

/// Run the Viterbi algorithm on runes[begin..end) and append segmented WordRanges to result.
inline auto hmm_internal_cut(const HMModel &model, std::span<const Rune> runes, uint32_t begin, uint32_t end,
                             std::vector<WordRange> &result, uint32_t pos) -> void {
    assert_check([&] { return begin < end && end <= runes.size(); }, "HMMSegment: invalid Viterbi rune range");
    assert_check([&] { return runes.size() <= std::numeric_limits<uint32_t>::max() - pos; },
                 "HMMSegment: global word offsets overflow");
    constexpr auto Y = kHMMStatesNum; // 4 states: B, E, M, S
    const auto X = static_cast<size_t>(end - begin);
    check(X <= std::numeric_limits<size_t>::max() / Y, "HMMSegment: input exceeds the Viterbi table size limit");

    // Flat 2D arrays laid out as [state * X + position] for cache-friendly access.
    auto weight = std::vector<double>(X * Y);
    auto path = std::vector<uint8_t>(X * Y);

    // ── Initialization (t = 0) ──────────────────────────────────────
    for (auto y = size_t{0}; y < Y; ++y) {
        weight[y * X] = model.get_start_prob(static_cast<HMMState>(y))
                        + model.get_emit_prob(static_cast<HMMState>(y), runes[begin]);
        assert_check([&] { return std::isfinite(weight[y * X]); }, "HMMSegment: non-finite initial weight");
        path[y * X] = 0;
    }

    // ── Recursion (t = 1 .. X-1) ────────────────────────────────────
    for (auto x = size_t{1}; x < X; ++x) {
        auto rune = runes[begin + x];
        for (auto y = size_t{0}; y < Y; ++y) {
            auto emit = model.get_emit_prob(static_cast<HMMState>(y), rune);
            auto best_weight = -std::numeric_limits<double>::infinity();
            // Default to E (End) state — matches the original cppjieba behavior.
            // When all transition weights tie at MIN_DOUBLE (e.g., for characters absent
            // from emit_prob_map), this default determines the backtrace path.
            // Missing runes are now emitted before Viterbi; the historical default no longer floors path scores.
            auto best_prev = static_cast<uint8_t>(HMMState::E);

            for (auto prev_y = size_t{0}; prev_y < Y; ++prev_y) {
                auto w = weight[prev_y * X + (x - 1)]
                         + model.get_trans_prob(static_cast<HMMState>(prev_y), static_cast<HMMState>(y)) + emit;
                assert_check([&] { return std::isfinite(w); }, "HMMSegment: non-finite accumulated weight");
                if (w > best_weight) {
                    best_weight = w;
                    best_prev = static_cast<uint8_t>(prev_y);
                }
            }

            assert_check([&] { return best_prev < Y && std::isfinite(best_weight); },
                         "HMMSegment: Viterbi must select a valid predecessor");
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
        assert_check([&] { return status[static_cast<size_t>(x + 1)] < Y; },
                     "HMMSegment: traceback state is out of range");
        status[static_cast<size_t>(x)] = path[status[static_cast<size_t>(x + 1)] * X + static_cast<size_t>(x + 1)];
    }

    // Emit word ranges based on E/S boundaries.
    auto word_begin = begin;
    for (auto i = size_t{0}; i < X; ++i) {
        assert_check([&] { return status[i] < Y; }, "HMMSegment: emitted state is out of range");
        auto state = static_cast<HMMState>(status[i]);
        if (state == HMMState::E || state == HMMState::S) {
            result.push_back(WordRange{pos + word_begin, pos + begin + static_cast<uint32_t>(i) + 1});
            word_begin = begin + static_cast<uint32_t>(i) + 1;
        }
    }
    assert_check([&] { return word_begin == end; }, "HMMSegment: words must cover the entire Viterbi range");
}

/// Find the end position of a consecutive ASCII letter sequence starting at `begin`.
/// Returns `begin` if runes[begin] is not a letter.
[[nodiscard]] constexpr auto sequential_letter_end(std::span<const Rune> runes, uint32_t begin, uint32_t end) noexcept
    -> uint32_t {
    assert_check([&] { return begin < end && end <= runes.size(); }, "HMMSegment: invalid ASCII letter range");
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
[[nodiscard]] constexpr auto number_end(std::span<const Rune> runes, uint32_t begin, uint32_t end) noexcept
    -> uint32_t {
    assert_check([&] { return begin < end && end <= runes.size(); }, "HMMSegment: invalid ASCII number range");
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

/// Distinguish absent runes from explicitly stored probabilities, including MIN_DOUBLE.
[[nodiscard]] inline auto hmm_has_emission(const HMModel &model, Rune rune) -> bool {
    for (auto state = size_t{0}; state < kHMMStatesNum; ++state) {
        if (model.get_emit_prob_map(static_cast<HMMState>(state)).contains(rune)) {
            return true;
        }
    }
    return false;
}

/// Append HMM segmentation results for a separator-free segment, preserving ASCII runs as whole tokens.
inline auto hmm_cut_one_segment(const HMModel &model, std::vector<WordRange> &range, std::span<const Rune> runes,
                                uint32_t pos) -> void {
    assert_check([&] { return runes.size() <= std::numeric_limits<uint32_t>::max() - pos; },
                 "HMMSegment: global word offsets overflow");
    if (runes.empty()) {
        return;
    }

    auto n = static_cast<uint32_t>(runes.size());

    auto left = uint32_t{0};
    auto right = uint32_t{0};

    while (right < n) {
        assert_check([&] { return left <= right; }, "HMMSegment: pending rune range is reversed");
        if (runes[right] < 0x80) {
            // Flush pending Chinese characters to HMM before handling ASCII.
            if (left < right) {
                hmm_internal_cut(model, runes, left, right, range, pos);
            }
            left = right;

            // Group ASCII words and numeric runs so they are not split by the HMM state machine.
            auto end = sequential_letter_end(runes, left, n);
            if (end == left) {
                end = number_end(runes, left, n);
            }
            if (end == left) {
                end = left + 1;
            }
            assert_check([&] { return left < end && end <= n; }, "HMMSegment: ASCII scan must advance in bounds");
            range.push_back(WordRange{pos + left, pos + end});
            right = end;
            left = right;
        } else if (!hmm_has_emission(model, runes[right])) {
            // An absent rune is a single-token boundary, independent of accumulated model scores.
            if (left < right) {
                hmm_internal_cut(model, runes, left, right, range, pos);
            }
            range.push_back(WordRange{pos + right, pos + right + 1});
            ++right;
            left = right;
        } else {
            ++right;
        }
    }

    // Flush the trailing Chinese run after the last ASCII span, if any.
    if (left < right) {
        hmm_internal_cut(model, runes, left, right, range, pos);
    }
}

/// Append HMM segmentation results while preserving separator runes as standalone tokens.
inline auto hmm_cut_append(const HMModel &model, std::span<const Rune> runes, std::vector<WordRange> &range,
                           uint32_t pos = 0) -> void {
    assert_check([&] { return runes.size() <= std::numeric_limits<uint32_t>::max() - pos; },
                 "HMMSegment: global word offsets overflow");
    auto segments = get_pre_filter_separators(runes);
    auto segment_pos = pos;
    // First text segment before the first separator.
    hmm_cut_one_segment(model, range, runes.subspan(0, segments[0]), segment_pos);
    for (auto i = size_t{0}; i < segments.size() - 1; ++i) {
        // Emit the separator rune itself.
        range.push_back(WordRange{pos + segments[i], pos + segments[i] + 1});
        auto next_begin = segments[i] + 1;
        segment_pos = pos + next_begin;
        // Continue with the following text segment.
        hmm_cut_one_segment(model, range, runes.subspan(next_begin, segments[i + 1] - next_begin), segment_pos);
    }
}

} // namespace detail

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
        check(runes.size() <= std::numeric_limits<uint32_t>::max(), "HMMSegment: input exceeds the word-range limit");
        auto range = std::vector<WordRange>{};
        range.reserve(runes.size() / 2);
        detail::hmm_cut_append(model, runes, range);
        return range;
    }
};

} // namespace neo_cppjieba
