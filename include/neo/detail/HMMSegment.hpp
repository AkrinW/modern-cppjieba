#pragma once

#include "neo/Config.hpp"
#include "neo/Unicode.hpp"
#include "neo/detail/HMModel.hpp"
#include "neo/detail/Logging.hpp"
#include "neo/detail/SegmentPolicy.hpp"
#include "neo/detail/SegmentScratch.hpp"
#include "neo/detail/StringUtil.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

namespace neo_cppjieba {

namespace detail {

// A reference HMM transition retains the selected predecessor along with its score.
struct HMMTransition {
    double weight;
    HMMState previous;
};

// Python and Rust consider only legal predecessors and select the higher state on a tie.
[[nodiscard]] inline auto style_hmm_transition(const HMModel &model, const EmitProbabilities &weights, double emit,
                                               HMMState state) noexcept -> HMMTransition {
    assert(static_cast<size_t>(state) < kHMMStatesNum);
    constexpr auto predecessors =
        std::array{std::array{HMMState::E, HMMState::S}, std::array{HMMState::B, HMMState::M},
                   std::array{HMMState::B, HMMState::M}, std::array{HMMState::E, HMMState::S}};
    const auto states = predecessors[static_cast<size_t>(state)];
    const auto first = weights[static_cast<size_t>(states[0])] + model.get_trans_prob(states[0], state) + emit;
    const auto second = weights[static_cast<size_t>(states[1])] + model.get_trans_prob(states[1], state) + emit;
    return first > second ? HMMTransition{first, states[0]} : HMMTransition{second, states[1]};
}

/// Run the Viterbi algorithm on runes[begin..end) and append segmented WordRanges to result.
template <typename Emit>
inline auto hmm_internal_cut(const HMModel &model, std::span<const Rune> runes, RuneIndex begin, RuneIndex end,
                             const Emit &emit_word, RuneIndex pos, SegmentScratch &scratch) -> void {
    assert_check([&] { return begin < end && end <= runes.size(); }, "HMMSegment: invalid Viterbi rune range");
    assert_check([&] { return runes.size() <= std::numeric_limits<RuneIndex>::max() - pos; },
                 "HMMSegment: global word offsets overflow");
    constexpr auto Y = kHMMStatesNum; // 4 states: B, E, M, S
    const auto X = static_cast<size_t>(end - begin);
    // A valid Rune span already bounds the storage needed for each row of HMM states.
    static_assert(Y <= sizeof(Rune));
    assert_check([&] { return X <= std::numeric_limits<size_t>::max() / Y; },
                 "HMMSegment: input exceeds the Viterbi table size limit");

    // Flat 2D arrays laid out as [state * X + position] for cache-friendly access.
    // Only paths retain this layout; scores need the previous and current columns.
    // These layout notes are historical; paths now use contiguous [position][state] rows.
    auto previous_weight = std::array<double, Y>{};
    auto current_weight = std::array<double, Y>{};
    // Every active row is overwritten before backtrace; retain the high-water size between HMM runs.
    if (scratch.hmm_path.size() < X) {
        scratch.hmm_path.resize(X);
    }
    // Keep the active row view stable after growth; Viterbi never resizes this storage.
    const auto path = std::span{scratch.hmm_path}.first(X);

    // ── Initialization (t = 0) ──────────────────────────────────────
    const auto first_emit = model.get_emit_probs(runes[begin]);
    for (auto y = size_t{0}; y < Y; ++y) {
        previous_weight[y] = model.get_start_prob(static_cast<HMMState>(y)) + first_emit[y];
        assert_check([&] { return std::isfinite(previous_weight[y]); }, "HMMSegment: non-finite initial weight");
    }
    path[0].fill(HMMState::B);

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
            auto best_prev = HMMState::E;

            if constexpr (compile_config::segmentation_style == SegmentationStyle::CPP) {
                for (auto prev_y = size_t{0}; prev_y < Y; ++prev_y) {
                    auto w = previous_weight[prev_y]
                             + model.get_trans_prob(static_cast<HMMState>(prev_y), static_cast<HMMState>(y)) + emit;
                    assert_check([&] { return std::isfinite(w); }, "HMMSegment: non-finite accumulated weight");
                    if (w > best_weight) {
                        best_weight = w;
                        best_prev = static_cast<HMMState>(prev_y);
                    }
                }
            } else {
                const auto transition = style_hmm_transition(model, previous_weight, emit, static_cast<HMMState>(y));
                best_weight = transition.weight;
                best_prev = transition.previous;
            }

            assert_check([&] { return static_cast<size_t>(best_prev) < Y && std::isfinite(best_weight); },
                         "HMMSegment: Viterbi must select a valid predecessor");
            current_weight[y] = best_weight;
            path[x][y] = best_prev;
        }
        previous_weight = current_weight;
    }

    // ── Termination: pick the best final state (must be E or S) ─────
    auto last = X - 1;
    auto end_weight = previous_weight[static_cast<size_t>(HMMState::E)];
    auto end_state = HMMState::E;
    auto s_weight = previous_weight[static_cast<size_t>(HMMState::S)];
    if (s_weight > end_weight
        || (compile_config::segmentation_style != SegmentationStyle::CPP && s_weight == end_weight)) {
        end_state = HMMState::S;
    }

    // ── Backtrace ───────────────────────────────────────────────────
    // Once a row's predecessor is read, that row is no longer needed for backtrace.
    // Reuse slot 0 for its decoded state so emitting words needs no separate status buffer.
    auto backtrace_state = end_state;
    for (auto x = last; x > 0; --x) {
        assert_check([&] { return static_cast<size_t>(backtrace_state) < Y; },
                     "HMMSegment: traceback state is out of range");
        const auto previous_state = path[x][static_cast<size_t>(backtrace_state)];
        path[x][0] = backtrace_state;
        backtrace_state = previous_state;
    }
    path[0][0] = backtrace_state;

    // Emit word ranges based on E/S boundaries.
    auto word_begin = begin;
    for (auto i = size_t{0}; i < X; ++i) {
        assert_check([&] { return static_cast<size_t>(path[i][0]) < Y; }, "HMMSegment: emitted state is out of range");
        const auto state = path[i][0];
        if constexpr (compile_config::segmentation_style != SegmentationStyle::CPP) {
            if (state == HMMState::B || state == HMMState::S) {
                word_begin = begin + static_cast<RuneIndex>(i);
            }
        }
        if (state == HMMState::E || state == HMMState::S) {
            emit_word(WordRange{static_cast<RuneIndex>(pos + word_begin), static_cast<RuneIndex>(pos + begin + i + 1)});
            word_begin = begin + static_cast<RuneIndex>(i) + 1;
        }
    }
    assert_check([&] { return word_begin == end; }, "HMMSegment: words must cover the entire Viterbi range");
}

/// Find the end position of a consecutive ASCII letter sequence starting at `begin`.
/// Returns `begin` if runes[begin] is not a letter.
[[nodiscard]] constexpr auto sequential_letter_end(std::span<const Rune> runes, RuneIndex begin, RuneIndex end) noexcept
    -> RuneIndex {
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
[[nodiscard]] constexpr auto number_end(std::span<const Rune> runes, RuneIndex begin, RuneIndex end) noexcept
    -> RuneIndex {
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

// Emit the reference HMM's alternating alphanumeric matches and non-Chinese gaps.
template <typename Emit>
inline auto style_hmm_emit_non_han(std::span<const Rune> runes, const Emit &emit_word, RuneIndex pos) -> void {
    auto begin = RuneIndex{0};
    while (begin < runes.size()) {
        auto end = begin;
        if (is_ascii_alphanumeric(runes[begin])) {
            end = style_hmm_ascii_end(runes, begin);
        } else {
            do {
                ++end;
            } while (end < runes.size() && !is_ascii_alphanumeric(runes[end]));
        }
        emit_word(WordRange{static_cast<RuneIndex>(pos + begin), static_cast<RuneIndex>(pos + end)});
        begin = end;
    }
}

// Isolate the reference HMM's Han blocks before running Viterbi, preserving source order.
template <typename Emit>
inline auto style_hmm_emit(const HMModel &model, const Emit &emit_word, std::span<const Rune> runes, RuneIndex pos,
                           SegmentScratch &scratch) -> void {
    auto begin = RuneIndex{0};
    while (begin < runes.size()) {
        const auto han = is_hmm_han(runes[begin]);
        auto end = static_cast<RuneIndex>(begin + 1);
        while (end < runes.size() && is_hmm_han(runes[end]) == han) {
            ++end;
        }
        if (!han) {
            style_hmm_emit_non_han(runes.subspan(begin, end - begin), emit_word, pos + begin);
        } else if (end - begin == 1) {
            emit_word(WordRange{static_cast<RuneIndex>(pos + begin), static_cast<RuneIndex>(pos + end)});
        } else {
            hmm_internal_cut(model, runes, begin, end, emit_word, pos, scratch);
        }
        begin = end;
    }
}

/// Append HMM segmentation results for a separator-free segment, preserving ASCII runs as whole tokens.
template <typename Emit>
inline auto hmm_emit_one_segment(const HMModel &model, const Emit &emit_word, std::span<const Rune> runes,
                                 RuneIndex pos, SegmentScratch &scratch) -> void {
    assert_check([&] { return runes.size() <= std::numeric_limits<RuneIndex>::max() - pos; },
                 "HMMSegment: global word offsets overflow");
    if (runes.empty()) {
        return;
    }

    if constexpr (compile_config::segmentation_style != SegmentationStyle::CPP) {
        style_hmm_emit(model, emit_word, runes, pos, scratch);
        return;
    }

    auto n = static_cast<RuneIndex>(runes.size());

    auto left = RuneIndex{0};
    auto right = RuneIndex{0};

    while (right < n) {
        assert_check([&] { return left <= right; }, "HMMSegment: pending rune range is reversed");
        if (runes[right] < 0x80) {
            // Flush pending Chinese characters to HMM before handling ASCII.
            if (left < right) {
                hmm_internal_cut(model, runes, left, right, emit_word, pos, scratch);
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
            emit_word(WordRange{static_cast<RuneIndex>(pos + left), static_cast<RuneIndex>(pos + end)});
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
        hmm_internal_cut(model, runes, left, right, emit_word, pos, scratch);
    }
}

/// Collect emitted HMM words in a caller-owned vector without changing its existing prefix.
inline auto hmm_cut_one_segment(const HMModel &model, std::vector<WordRange> &range, std::span<const Rune> runes,
                                RuneIndex pos, SegmentScratch &scratch) -> void {
    const auto emit_word = [&](WordRange word) {
        range.push_back(word);
    };
    hmm_emit_one_segment(model, emit_word, runes, pos, scratch);
}

/// Append HMM segmentation results while preserving separator runes as standalone tokens.
inline auto hmm_cut_append(const HMModel &model, std::span<const Rune> runes, std::vector<WordRange> &range,
                           RuneIndex pos, SegmentScratch &scratch) -> void {
    assert_check([&] { return runes.size() <= std::numeric_limits<RuneIndex>::max() - pos; },
                 "HMMSegment: global word offsets overflow");
    if constexpr (compile_config::segmentation_style != SegmentationStyle::CPP) {
        hmm_cut_one_segment(model, range, runes, pos, scratch);
        return;
    }
    get_pre_filter_separators(runes, scratch.separators);
    const auto &segments = scratch.separators;
    auto segment_pos = pos;
    // First text segment before the first separator.
    hmm_cut_one_segment(model, range, runes.subspan(0, segments[0]), segment_pos, scratch);
    for (auto i = size_t{0}; i < segments.size() - 1; ++i) {
        // Emit the separator rune itself.
        range.push_back(
            WordRange{static_cast<RuneIndex>(pos + segments[i]), static_cast<RuneIndex>(pos + segments[i] + 1)});
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
        assert_check([&] { return runes.size() <= std::numeric_limits<RuneIndex>::max(); },
                     "HMMSegment: input exceeds the word-range limit");
        range.clear();
        range.reserve(runes.size() / 2);
        detail::hmm_cut_append(model, runes, range, 0, scratch);
    }
};

} // namespace neo_cppjieba
