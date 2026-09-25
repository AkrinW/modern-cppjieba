#pragma once

#include "neo/Unicode.hpp"
#include "neo/detail/HMModel.hpp"
#include "neo/detail/Logging.hpp"
#include "neo/detail/SegmentScratch.hpp"
#include "neo/detail/StringUtil.hpp"

#include <array>
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
                             std::vector<WordRange> &result, uint32_t pos, SegmentScratch &scratch) -> void {
    assert_check([&] { return begin < end && end <= runes.size(); }, "HMMSegment: invalid Viterbi rune range");
    assert_check([&] { return runes.size() <= std::numeric_limits<uint32_t>::max() - pos; },
                 "HMMSegment: global word offsets overflow");
    constexpr auto Y = kHMMStatesNum; // 4 states: B, E, M, S
    const auto X = static_cast<size_t>(end - begin);
    check(X <= std::numeric_limits<size_t>::max() / Y, "HMMSegment: input exceeds the Viterbi table size limit");

    // Flat 2D arrays laid out as [state * X + position] for cache-friendly access.
    // Only paths retain this layout; scores need the previous and current columns.
    // These layout notes are historical; paths now use contiguous [position][state] rows.
    auto previous_weight = std::array<double, Y>{};
    auto current_weight = std::array<double, Y>{};
    auto &path = scratch.hmm_path;
    // Every active row is overwritten before backtrace; retain the high-water size between HMM runs.
    if (path.size() < X) {
        path.resize(X);
    }

    // ── Initialization (t = 0) ──────────────────────────────────────
    const auto first_emit = model.get_emit_probs(runes[begin]);
    for (auto y = size_t{0}; y < Y; ++y) {
        previous_weight[y] = model.get_start_prob(static_cast<HMMState>(y)) + first_emit[y];
        assert_check([&] { return std::isfinite(previous_weight[y]); }, "HMMSegment: non-finite initial weight");
        path[0][y] = 0;
    }

    // ── Recursion (t = 1 .. X-1) ────────────────────────────────────
    for (auto x = size_t{1}; x < X; ++x) {
        const auto emit_probs = model.get_emit_probs(runes[begin + x]);
        for (auto y = size_t{0}; y < Y; ++y) {
            const auto emit = emit_probs[y];
            auto best_weight = MIN_DOUBLE;
            // Default to E (End) state — matches the original cppjieba behavior.
            // When all transition weights tie at MIN_DOUBLE (e.g., for characters absent
            // from emit_prob_map), this default determines the backtrace path.

            // Missing runes are now emitted before Viterbi; the historical default no longer floors path scores.
            // That policy is retired: preserve the legacy score floor and E predecessor for compatibility.
            auto best_prev = static_cast<uint8_t>(HMMState::E);

            for (auto prev_y = size_t{0}; prev_y < Y; ++prev_y) {
                auto w = previous_weight[prev_y]
                         + model.get_trans_prob(static_cast<HMMState>(prev_y), static_cast<HMMState>(y)) + emit;
                assert_check([&] { return std::isfinite(w); }, "HMMSegment: non-finite accumulated weight");
                if (w > best_weight) {
                    best_weight = w;
                    best_prev = static_cast<uint8_t>(prev_y);
                }
            }

            assert_check([&] { return best_prev < Y && std::isfinite(best_weight); },
                         "HMMSegment: Viterbi must select a valid predecessor");
            current_weight[y] = best_weight;
            path[x][y] = best_prev;
        }
        previous_weight = current_weight;
    }

    // ── Termination: pick the best final state (must be E or S) ─────
    auto last = X - 1;
    auto end_weight = previous_weight[static_cast<size_t>(HMMState::E)];
    auto end_state = uint8_t{static_cast<uint8_t>(HMMState::E)};
    auto s_weight = previous_weight[static_cast<size_t>(HMMState::S)];
    if (s_weight > end_weight) {
        end_state = static_cast<uint8_t>(HMMState::S);
    }

    // ── Backtrace ───────────────────────────────────────────────────
    // Once a row's predecessor is read, that row is no longer needed for backtrace.
    // Reuse slot 0 for its decoded state so emitting words needs no separate status buffer.
    auto backtrace_state = end_state;
    for (auto x = last; x > 0; --x) {
        assert_check([&] { return backtrace_state < Y; }, "HMMSegment: traceback state is out of range");
        const auto previous_state = path[x][backtrace_state];
        path[x][0] = backtrace_state;
        backtrace_state = previous_state;
    }
    path[0][0] = backtrace_state;

    // Emit word ranges based on E/S boundaries.
    auto word_begin = begin;
    for (auto i = size_t{0}; i < X; ++i) {
        assert_check([&] { return path[i][0] < Y; }, "HMMSegment: emitted state is out of range");
        auto state = static_cast<HMMState>(path[i][0]);
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
// The separate missing-rune classifier is retired; legacy Viterbi handles those runes directly.

/// Append HMM segmentation results for a separator-free segment, preserving ASCII runs as whole tokens.
inline auto hmm_cut_one_segment(const HMModel &model, std::vector<WordRange> &range, std::span<const Rune> runes,
                                uint32_t pos, SegmentScratch &scratch) -> void {
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
                hmm_internal_cut(model, runes, left, right, range, pos, scratch);
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
        } else {
            // An absent rune is a single-token boundary, independent of accumulated model scores.
            // That policy is retired: unknown runes remain inside the current Viterbi span.
            ++right;
        }
    }

    // Flush the trailing Chinese run after the last ASCII span, if any.
    if (left < right) {
        hmm_internal_cut(model, runes, left, right, range, pos, scratch);
    }
}

/// Append HMM segmentation results while preserving separator runes as standalone tokens.
inline auto hmm_cut_append(const HMModel &model, std::span<const Rune> runes, std::vector<WordRange> &range,
                           uint32_t pos, SegmentScratch &scratch) -> void {
    assert_check([&] { return runes.size() <= std::numeric_limits<uint32_t>::max() - pos; },
                 "HMMSegment: global word offsets overflow");
    get_pre_filter_separators(runes, scratch.separators);
    const auto &segments = scratch.separators;
    auto segment_pos = pos;
    // First text segment before the first separator.
    hmm_cut_one_segment(model, range, runes.subspan(0, segments[0]), segment_pos, scratch);
    for (auto i = size_t{0}; i < segments.size() - 1; ++i) {
        // Emit the separator rune itself.
        range.push_back(WordRange{pos + segments[i], pos + segments[i] + 1});
        auto next_begin = segments[i] + 1;
        segment_pos = pos + next_begin;
        // Continue with the following text segment.
        hmm_cut_one_segment(model, range, runes.subspan(next_begin, segments[i + 1] - next_begin), segment_pos,
                            scratch);
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
        auto range = std::vector<WordRange>{};
        auto scratch = detail::SegmentScratch{};
        cut_into(model, runes, range, scratch);
        return range;
    }

    // Replace output and reuse predecessor storage across HMM runs and calls.
    static auto cut_into(const HMModel &model, std::span<const Rune> runes, std::vector<WordRange> &range,
                         detail::SegmentScratch &scratch) -> void {
        check(runes.size() <= std::numeric_limits<uint32_t>::max(), "HMMSegment: input exceeds the word-range limit");
        range.clear();
        range.reserve(runes.size() / 2);
        detail::hmm_cut_append(model, runes, range, 0, scratch);
    }
};

} // namespace neo_cppjieba
