#pragma once

#include "neo/Unicode.hpp"
#include "neo/detail/FileIO.hpp"
#include "neo/detail/Logging.hpp"
#include "neo/detail/StringUtil.hpp"
#include "neo/detail/Trie.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace neo_cppjieba {

class DictTrie {
public:
    enum class UserWordWeightOption : uint8_t { WordWeightMin, WordWeightMedian, WordWeightMax };

    /// Construct from a main dictionary file and an optional user dictionary file.
    ///
    /// @param dict_path             path to the main dictionary (3-column: "word freq tag")
    /// @param user_dict_path        path to a single user dictionary file (empty = none)
    /// @param user_word_weight_opt  strategy for default weight of user words without freq
    explicit DictTrie(std::string_view dict_path, std::string_view user_dict_path = "",
                      UserWordWeightOption user_word_weight_opt = UserWordWeightOption::WordWeightMedian) {
        init(dict_path, user_dict_path, user_word_weight_opt);
    }

    // ── Query interface ──────────────────────────────────────────────────

    /// Find exact match by rune span.
    [[nodiscard]] auto find(std::span<const Rune> key) const -> DictUnit {
        return trie_.find(key);
    }

    /// Find exact match by any StringLike type (UTF-8, UTF-16, etc.).
    template <StringLike T>
    [[nodiscard]] auto find(const T &input) const -> DictUnit {
        auto unicode = decode(input);
        return find(std::span<const Rune>{unicode});
    }

    [[nodiscard]] auto find_dag(std::span<const Rune> runes) const -> Dag {
        return trie_.find_dag(runes);
    }

    // Reuse a caller-owned DAG without changing dictionary matching semantics.
    auto find_dag_into(std::span<const Rune> runes, Dag &dag) const -> void {
        trie_.find_dag_into(runes, dag);
    }

    template <StringLike T>
    [[nodiscard]] auto find_dag(const T &input) const -> Dag {
        auto unicode = decode(input);
        return find_dag(std::span<const Rune>{unicode});
    }

    /// Check if a single Chinese character comes from the user dictionary.
    [[nodiscard]] auto is_user_dict_single_chinese_word(Rune word) const -> bool {
        return user_dict_single_chinese_word_.contains(word);
    }

    [[nodiscard]] auto min_weight() const noexcept -> float {
        return min_weight_;
    }
    [[nodiscard]] auto max_weight() const noexcept -> float {
        return max_weight_;
    }
    [[nodiscard]] auto median_weight() const noexcept -> float {
        return median_weight_;
    }
    [[nodiscard]] auto user_word_default_weight() const noexcept -> float {
        return user_word_default_weight_;
    }
    [[nodiscard]] auto freq_sum() const noexcept -> float {
        return freq_sum_;
    }
    [[nodiscard]] auto trie() const noexcept -> const Trie & {
        return trie_;
    }

private:
    // ── Intermediate representation ──────────────────────────────────────
    //
    // Holds raw integer frequency during dictionary loading.
    // Discarded after the Trie is built — all final DictUnit values live
    // inside the Trie (no external std::vector / std::deque needed).

    struct RawEntry {
        Unicode word;
        int freq; // 0 ⇒ "use default weight" (user words without explicit freq)
        PosTag tag;
    };

    // ── Core initialisation ──────────────────────────────────────────────
    //
    // Pipeline:  load dicts → stats on int freqs → convert → build trie
    //
    // Key performance improvements over the original DictTrie::Init:
    //
    //   1. buffered reads + zero-copy parsing (FileBuffer / lines_view / split_view)
    //      instead of ifstream + getline + limonp::Split.
    //
    //   2. Weight statistics computed on raw int frequencies:
    //        • min/max via std::ranges::minmax_element  — O(n)
    //        • median via std::ranges::nth_element       — O(n) average
    //      The original code sorted a *copy* of all DictUnit by double weight
    //      (O(n log n) + heavy copy), then picked min/max/median.
    //      Since log() is strictly monotonic, the results are identical.
    //
    //   3. No external DictUnit storage — the Trie owns everything inline.
    //      Eliminates std::vector<DictUnit> + std::deque<DictUnit> + pointer
    //      indirections that the original design required.

    auto init(std::string_view dict_path, std::string_view user_dict_path, UserWordWeightOption opt) -> void {

        // ── Phase 1: Load main dictionary ────────────────────────────────
        auto entries = std::vector<RawEntry>{};
        auto freqs = std::vector<int>{}; // parallel to entries (main dict only)
        load_main_dict(dict_path, entries, freqs);
        check(!entries.empty(), "DictTrie: main dictionary is empty: {}", dict_path);
        assert_check([&] { return entries.size() == freqs.size(); },
                     "DictTrie: main dictionary entries and frequencies must have equal sizes");

        // ── Phase 2: Compute statistics on integer frequencies ───────────
        auto freq_sum = int64_t{0};
        for (auto f : freqs) {
            assert_check([=] { return f > 0; }, "DictTrie: unchecked main dictionary frequency {}", f);
            check(f <= std::numeric_limits<int64_t>::max() - freq_sum,
                  "DictTrie: frequency sum exceeds the supported range: {}", dict_path);
            freq_sum += f;
        }
        check(freq_sum > 0, "DictTrie: frequency sum must be positive");
        freq_sum_ = static_cast<float>(freq_sum);

        // min / max — O(n), single pass
        auto &&[min_it, max_it] = std::ranges::minmax_element(freqs);
        auto min_freq = *min_it;
        auto max_freq = *max_it;

        // median — O(n) average via nth_element (mutates freqs, but we
        // already extracted min/max values above)
        auto mid = static_cast<std::ptrdiff_t>(freqs.size() / 2);
        std::ranges::nth_element(freqs, freqs.begin() + mid);
        auto median_freq = freqs[mid];

        // Convert selected int frequencies to log-weights
        auto to_weight = [&](int f) -> float {
            assert_check([&] { return f > 0 && std::isfinite(freq_sum_) && freq_sum_ > 0.0f; },
                         "DictTrie: invalid frequency normalization");
            return std::log(f / freq_sum_);
        };

        min_weight_ = to_weight(min_freq);
        max_weight_ = to_weight(max_freq);
        median_weight_ = to_weight(median_freq);
        assert_check([&] { return min_weight_ <= median_weight_ && median_weight_ <= max_weight_; },
                     "DictTrie: inconsistent weight statistics");
        set_user_default_weight(opt);

        // ── Phase 3: Load user dictionary entries ────────────────────────
        if (!user_dict_path.empty()) {
            load_user_dict_file(user_dict_path, entries);
        }

        // ── Phase 4: Build Trie from all entries ─────────────────────────
        //
        // Convert each RawEntry to (Unicode key, DictUnit value) and hand
        // them to Trie::build(). After this call the temporary vectors are
        // freed; the Trie owns all DictUnit data inline.

        auto keys = std::vector<Unicode>{};
        auto values = std::vector<DictUnit>{};
        keys.reserve(entries.size());
        values.reserve(entries.size());

        for (auto &e : entries) {
            auto w = e.freq > 0 ? to_weight(e.freq) : user_word_default_weight_;
            keys.push_back(std::move(e.word));
            values.push_back(DictUnit{w, e.tag});
        }

        trie_.build(keys, values);
    }

    // ── Main dictionary loading ──────────────────────────────────────────
    //
    // Each line has exactly 3 space-separated fields: "word freq tag"
    // e.g.  "AT&T 3 nz",  "我们 � r"

    auto load_main_dict(std::string_view path, std::vector<RawEntry> &entries, std::vector<int> &freqs) -> void {
        auto file = read_file(path);
        auto line = get_line_view(file.content());
        auto line_number = size_t{0};

        for (auto &&line_view : line) {
            ++line_number;
            if (line_view.empty()) {
                continue;
            }

            auto sv = get_split_view(line_view, ' ');
            auto it = sv.begin();
            const auto end = sv.end();

            assert_check([&] { return it != end; }, "DictTrie: a nonempty line must contain a field");
            auto word_sv = *it;
            ++it;
            check(it != end, "DictTrie: missing frequency at {}:{}", path, line_number);
            auto freq_sv = *it;
            ++it;
            check(it != end, "DictTrie: missing tag at {}:{}", path, line_number);
            auto tag_sv = *it;
            ++it;
            check(it == end, "DictTrie: expected exactly 3 fields at {}:{}", path, line_number);

            auto word = decode(word_sv);
            if (word.empty()) {
                continue;
            }

            auto freq = decode_value<int>(freq_sv);
            check(freq > 0, "DictTrie: frequency must be positive at {}:{}, got {}", path, line_number, freq);

            entries.push_back(RawEntry{std::move(word), freq, PosTag{tag_sv}});
            freqs.push_back(freq);
        }
    }

    /// Load a single user dictionary file.
    ///
    /// Supported line formats (fields separated by space):
    ///   1 field  — "word"            → default weight, empty tag
    ///   2 fields — "word tag"        → default weight, explicit tag
    ///   3 fields — "word freq tag"   → weight from freq, explicit tag
    auto load_user_dict_file(std::string_view path, std::vector<RawEntry> &entries) -> void {
        auto file = read_file(path);
        auto line = get_line_view(file.content());
        for (auto &&one_line : line) {
            if (one_line.empty()) {
                continue;
            }
            parse_user_dict_line(one_line, entries);
        }
    }

    auto parse_user_dict_line(std::string_view line, std::vector<RawEntry> &entries) -> void {
        auto sv = split_view{line, ' '};
        auto it = sv.begin();
        auto end = sv.end();

        if (it == end) {
            return;
        }

        auto word_sv = *it;
        ++it;
        auto word = decode(word_sv);
        if (word.empty()) {
            return;
        }

        auto freq = int{0}; // 0 → "use default weight"
        auto tag = PosTag{};

        if (it != end) {
            auto second = *it;
            ++it;
            if (it != end) {
                // 3 fields: word  freq  tag
                freq = decode_value<int>(second);
                check(freq >= 0, "DictTrie: user dictionary frequency must not be negative: '{}'", line);
                tag = PosTag{*it};
                ++it;
                check(it == end, "DictTrie: expected at most 3 user dictionary fields: '{}'", line);
            } else {
                // 2 fields: word  tag
                tag = PosTag{second};
            }
        }

        if (word.size() == 1) {
            user_dict_single_chinese_word_.insert(word[0]);
        }

        entries.push_back(RawEntry{std::move(word), freq, tag});
    }

    auto set_user_default_weight(UserWordWeightOption opt) -> void {
        switch (opt) {
            case UserWordWeightOption::WordWeightMin: {
                user_word_default_weight_ = min_weight_;
                break;
            }
            case UserWordWeightOption::WordWeightMedian: {
                user_word_default_weight_ = median_weight_;
                break;
            }
            case UserWordWeightOption::WordWeightMax: {
                user_word_default_weight_ = max_weight_;
                break;
            }
            default: {
                check(false, "DictTrie: invalid user word weight option {}", static_cast<uint8_t>(opt));
                break;
            }
        }
    }

    // ── Data members ─────────────────────────────────────────────────────
    //
    // No external DictUnit storage: the Trie stores all values inline.
    // The only heap state is the Trie's node vector, the single-char set,
    // and the handful of scalar statistics below.

    Trie trie_;
    float freq_sum_{0.0f};
    float min_weight_{0.0f};
    float max_weight_{0.0f};
    float median_weight_{0.0f};
    float user_word_default_weight_{0.0f};
    std::unordered_set<Rune> user_dict_single_chinese_word_;
};

} // namespace neo_cppjieba
