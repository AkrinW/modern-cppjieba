#pragma once

#include "FileIO.hpp"
#include "Logging.hpp"
#include "StringUtil.hpp"
#include "Unicode.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace neo_cppjieba {

/// Emit probability map: maps a single Unicode code point (Rune) to its log-probability.
using EmitProbMap = std::unordered_map<Rune, double>;

inline constexpr auto MIN_DOUBLE = -3.14e+100;
inline constexpr auto MAX_DOUBLE = 3.14e+100;

// HMMState represents the hidden states in the HMM model for Chinese word segmentation.
enum class HMMState : uint8_t {
    B = 0, // Begin
    E = 1, // End
    M = 2, // Middle
    S = 3  // Single
};
inline constexpr auto kHMMStatesNum = size_t{4};
inline constexpr auto kHMMStateLables = std::array<char, kHMMStatesNum>{'B', 'E', 'M', 'S'};

constexpr auto get_hmm_state_label(HMMState state) -> char {
    return kHMMStateLables[static_cast<size_t>(state)];
}

/// HMModel loads a Hidden Markov Model for Chinese word segmentation.
///
/// The model contains four hidden states — B (Begin), E (End), M (Middle), S (Single) —
/// and three sets of parameters:
///   - start probabilities   (initial state distribution)
///   - transition probabilities (state-to-state matrix)
///   - emission probabilities  (state → observed character)
///
/// File format (dict/hmm_model.utf8):
///   - Lines starting with '#' and blank lines are skipped.
///   - Line 1:  start probabilities (4 space-separated doubles)
///   - Lines 2–5: transition probability matrix (4 × 4, space-separated)
///   - Lines 6–9: emission probabilities for B, E, M, S respectively
///               (comma-separated "char:prob" pairs, e.g. "我:-4.711846,的:-6.412328")
struct HMModel {
    // ── Constructors ─────────────────────────────────────────────────────
    /// Construct and load the model from the given file path.
    explicit HMModel(std::string_view model_path) {
        load(model_path);
    }

    constexpr auto get_start_prob(HMMState state) const -> double {
        return start_prob[static_cast<size_t>(state)];
    }

    constexpr auto get_trans_prob(HMMState from, HMMState to) const -> double {
        return trans_prob[static_cast<size_t>(from)][static_cast<size_t>(to)];
    }

    constexpr auto get_emit_prob_map(HMMState state) const -> const EmitProbMap & {
        return emit_prob_maps[static_cast<size_t>(state)];
    }

    // ── Query interface ──────────────────────────────────────────────────

    /// Look up the emission probability of `key` from the given emit-prob map.
    /// Returns `default_val` when the key is absent.
    [[nodiscard]] auto get_emit_prob(HMMState state, Rune key) const noexcept -> double {
        const auto &mp = get_emit_prob_map(state);
        if (auto it = mp.find(key); it != mp.end()) {
            return it->second;
        }
        return MIN_DOUBLE;
    }

    // ── Model parameters (public for direct access by HMSegment) ─────────

    std::array<double, kHMMStatesNum> start_prob{};
    std::array<std::array<double, kHMMStatesNum>, kHMMStatesNum> trans_prob{};
    std::array<EmitProbMap, kHMMStatesNum> emit_prob_maps; // indexed access to emit_prob maps
private:
    // ── Loading ──────────────────────────────────────────────────────────
    auto load(std::string_view model_path) -> void {
        auto file = read_file(model_path);
        auto lines = get_line_view(file.content());

        // Collect non-comment, non-empty lines.
        auto data_lines = std::vector<std::string_view>{};
        // 4 (startProb) + 4 (transProb rows) + 4 (emitProb) = up to ~12 data lines, but
        // startProb is 1 line, so total 9 data lines minimum.
        data_lines.reserve(16);

        for (auto &&line : lines) {
            auto trimmed = trim(line);
            if (trimmed.empty() || trimmed.front() == '#') {
                continue;
            }
            data_lines.push_back(trimmed);
        }

        // We expect at least 9 data lines:
        //   1 (startProb) + 4 (transProb) + 4 (emitProb B/E/M/S)
        check(data_lines.size() >= 9, "HMModel: expected >= 9 data lines in {}, got {}", model_path, data_lines.size());

        auto idx = size_t{0};

        // ── Start probabilities ──────────────────────────────────────────
        start_prob = parse_prob_line(data_lines[idx]);
        ++idx;

        // ── Transition probabilities ─────────────────────────────────────
        for (auto i = size_t{0}; i < kHMMStatesNum; ++i) {
            trans_prob[i] = parse_prob_line(data_lines[idx]);
            ++idx;
        }

        // ── Emission probabilities ───────────────────────────────────────
        for (auto i = size_t{0}; i < kHMMStatesNum; ++i) {
            emit_prob_maps[i] = parse_emit_prob_map(data_lines[idx]);

            log<LogLevel::LL_INFO>("HMModel: loaded emit map state {} with {} entries, avg_load_factor {:.2f}",
                                   get_hmm_state_label(static_cast<HMMState>(i)), emit_prob_maps[i].size(),
                                   emit_prob_maps[i].load_factor());
            ++idx;
        }
    }

    /// Parse a space-separated line of doubles into a fixed-size array.
    auto parse_prob_line(std::string_view line) -> std::array<double, kHMMStatesNum> {
        auto out = std::array<double, kHMMStatesNum>{};
        auto i = size_t{0};
        auto split = get_split_view(line, ' ');
        for (auto &&token : split) {
            if (token.empty()) {
                continue;
            }
            check(i < kHMMStatesNum, "HMModel: too many values in probability line");
            out[i] = decode_value<double>(token);
            ++i;
        }
        check(i == kHMMStatesNum, "HMModel: expected {} values in probability line, got {}", kHMMStatesNum, i);
        return out;
    }

    /// Parse an emit-probability line: comma-separated "char:prob" pairs.
    auto parse_emit_prob_map(std::string_view line) -> EmitProbMap {
        check(!line.empty(), "HMModel: empty emit probability line");
        auto mp = EmitProbMap{};
        auto split = get_split_view(line, ',');
        for (auto &&pair : split) {
            if (pair.empty()) {
                continue;
            }
            auto colon_pos = pair.find(':');
            check(colon_pos != std::string_view::npos, "HMModel: malformed emit prob entry (no ':'): {}", pair);

            auto char_sv = pair.substr(0, colon_pos);
            auto prob_sv = pair.substr(colon_pos + 1);

            auto unicode = decode(char_sv);
            check(unicode.size() == 1, "HMModel: emit prob key must be a single character, got '{}' ({} runes)",
                  char_sv, unicode.size());

            mp.emplace(unicode[0], decode_value<double>(prob_sv));
        }
        mp.reserve(mp.size() * 4); // reserve space to increase query performance by avoiding rehashing.
        return mp;
    }
}; // struct HMModel

} // namespace neo_cppjieba
