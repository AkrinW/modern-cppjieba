#pragma once

#include "neo/DictSource.hpp"
#include "neo/Unicode.hpp"
#include "neo/detail/Logging.hpp"
#include "neo/detail/StringUtil.hpp"
#include "neo/detail/Trie.hpp"
#include "neo/third_party/gtl.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace neo_cppjieba::detail {

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
        check_weight_option(user_word_weight_opt);
        auto user_dicts = std::vector<DictSource>{};
        if (!user_dict_path.empty()) {
            user_dicts.push_back(DictSource::file(user_dict_path));
        }
        init(DictSource::file(dict_path), user_dicts, user_word_weight_opt);
    }

    /// Construct from file or memory sources. User dictionaries load in order; for a repeated word
    /// the last entry wins. Weight statistics come from the main dictionary only.
    explicit DictTrie(const DictSource &dict, std::span<const DictSource> user_dicts = {},
                      UserWordWeightOption user_word_weight_opt = UserWordWeightOption::WordWeightMedian) {
        check_weight_option(user_word_weight_opt);
        init(dict, user_dicts, user_word_weight_opt);
    }

    // ── Query interface ──────────────────────────────────────────────────

    /// Find exact match by rune span.
    [[nodiscard]] auto find(std::span<const Rune> key) const noexcept -> DictUnit {
        return trie_.find(key);
    }

    /// Find exact match by any StringLike type (UTF-8, UTF-16, etc.).
    template <StringLike T>
    [[nodiscard]] auto find(const T &input) const -> DictUnit {
        auto unicode = decode(input);
        return find(std::span<const Rune>{unicode});
    }

    template <typename Emit>
    auto for_each_match_from(std::span<const Rune> runes, RuneIndex begin, Emit &&emit) const
        noexcept(noexcept(trie_.for_each_match_from(runes, begin, std::forward<Emit>(emit)))) -> void {
        trie_.for_each_match_from(runes, begin, std::forward<Emit>(emit));
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
    /// MIX and SEARCH ask this for every single-rune route word, and most user dictionaries add none.
    [[nodiscard]] auto is_user_dict_single_chinese_word(Rune word) const noexcept -> bool {
        return !user_dict_single_chinese_word_.empty() && user_dict_single_chinese_word_.contains(word);
    }

    [[nodiscard]] auto min_weight() const noexcept -> detail::WordWeight {
        return min_weight_;
    }
    [[nodiscard]] auto max_weight() const noexcept -> detail::WordWeight {
        return max_weight_;
    }
    [[nodiscard]] auto median_weight() const noexcept -> detail::WordWeight {
        return median_weight_;
    }
    [[nodiscard]] auto user_word_default_weight() const noexcept -> detail::WordWeight {
        return user_word_default_weight_;
    }
    [[nodiscard]] auto freq_sum() const noexcept -> detail::WordWeight {
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

    static auto check_weight_option(UserWordWeightOption opt) -> void {
        check(opt == UserWordWeightOption::WordWeightMin || opt == UserWordWeightOption::WordWeightMedian
                  || opt == UserWordWeightOption::WordWeightMax,
              "DictTrie: invalid user word weight option {}", static_cast<uint8_t>(opt));
    }

    // ── Core initialisation ──────────────────────────────────────────────
    //
    // Pipeline: load dicts → stats on int freqs → convert → build trie.
    // Statistics use integer frequencies (minmax_element, nth_element) in O(n);
    // log() is strictly monotonic, so they equal CppJieba's sorted-weight results.

    auto init(const DictSource &dict, std::span<const DictSource> user_dicts, UserWordWeightOption opt) -> void {

        // ── Phase 1: Load main dictionary ────────────────────────────────
        auto entries = std::vector<RawEntry>{};
        auto freqs = std::vector<int>{}; // parallel to entries (main dict only)
        detail::with_source_text(
            dict, [&](std::string_view text, std::string_view name) { load_main_dict(text, name, entries, freqs); });
        check(!entries.empty(), "DictTrie: main dictionary is empty: {}", dict.name());
        assert_check([&] { return entries.size() == freqs.size(); },
                     "DictTrie: main dictionary entries and frequencies must have equal sizes");

        // ── Phase 2: Compute statistics on integer frequencies ───────────
        auto freq_sum = int64_t{0};
        for (auto f : freqs) {
            assert_check([=] { return f > 0; }, "DictTrie: unchecked main dictionary frequency {}", f);
            check(f <= std::numeric_limits<int64_t>::max() - freq_sum,
                  "DictTrie: frequency sum exceeds the supported range: {}", dict.name());
            freq_sum += f;
        }
        assert_check([&] { return freq_sum > 0; }, "DictTrie: frequency sum must be positive");
        freq_sum_ = static_cast<detail::WordWeight>(freq_sum);

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
        auto to_weight = [&](int f) -> detail::WordWeight {
            assert_check([&] { return f > 0 && std::isfinite(freq_sum_) && freq_sum_ > 0.0; },
                         "DictTrie: invalid frequency normalization");
            if constexpr (compile_config::segmentation_style == SegmentationStyle::CPP) {
                return std::log(f / freq_sum_);
            } else {
                return std::log(static_cast<double>(f)) - std::log(freq_sum_);
            }
        };

        min_weight_ = to_weight(min_freq);
        max_weight_ = to_weight(max_freq);
        median_weight_ = to_weight(median_freq);
        assert_check([&] { return min_weight_ <= median_weight_ && median_weight_ <= max_weight_; },
                     "DictTrie: inconsistent weight statistics");
        set_user_default_weight(opt);

        // ── Phase 3: Load user dictionary entries ────────────────────────
        for (const auto &user_dict : user_dicts) {
            detail::with_source_text(user_dict,
                                     [&](std::string_view text, std::string_view) { load_user_dict(text, entries); });
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

    auto load_main_dict(std::string_view text, std::string_view path, std::vector<RawEntry> &entries,
                        std::vector<int> &freqs) -> void {
        auto line = get_line_view(text);
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

    /// Load the text of a single user dictionary.
    ///
    /// Supported line formats (fields separated by space):
    ///   1 field  — "word"            → default weight, empty tag
    ///   2 fields — "word tag"        → default weight, explicit tag
    ///   3 fields — "word freq tag"   → weight from freq, explicit tag
    auto load_user_dict(std::string_view text, std::vector<RawEntry> &entries) -> void {
        auto line = get_line_view(text);
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

    auto set_user_default_weight(UserWordWeightOption opt) noexcept -> void {
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
                assert_check([] { return false; }, "DictTrie: unchecked user word weight option {}",
                             static_cast<uint8_t>(opt));
                std::unreachable();
            }
        }
    }

    // ── Data members ─────────────────────────────────────────────────────
    // The Trie stores every DictUnit inline; no separate entry storage is kept.

    Trie trie_;
    detail::WordWeight freq_sum_{0.0};
    detail::WordWeight min_weight_{0.0};
    detail::WordWeight max_weight_{0.0};
    detail::WordWeight median_weight_{0.0};
    detail::WordWeight user_word_default_weight_{0.0};
    gtl::flat_hash_set<Rune> user_dict_single_chinese_word_;
};

} // namespace neo_cppjieba::detail
