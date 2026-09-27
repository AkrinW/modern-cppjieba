/// Benchmark: Compare five segmentation methods (Old vs Neo) via Jieba
///
/// Loads a Chinese text file, then:
///   1. Verifies that old (cppjieba) and neo (neo_cppjieba) produce identical
///      segmentation results for each of the five methods.
///   2. Benchmarks the Neo core segmentation throughput (Unicode-level,
///      pre-decoded, excludes decode/encode) through a single Jieba instance.
///   3. Benchmarks Old vs Neo at the string-level (full pipeline: decode →
///      pre-filter → cut → encode) for a fair comparison.
///
/// Five segmentation methods:
///   MIX    — Dictionary (MP) + HMM  (recommended default)
///   MP     — Pure dictionary (maximum probability, no HMM)
///   HMM    — Pure HMM (Viterbi only, no dictionary)
///   FULL   — All dictionary-matching words (overlapping)
///   SEARCH — MIX + sub-word extraction (for search engines)

// ── Old headers ──
#include "cppjieba/Jieba.hpp"

// ── Neo headers ──
#include "neo/Jieba.hpp"
#include "neo/Unicode.hpp"

#include "BenchmarkUtils.hpp"
#include "test_paths.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct BenchResult {
    const char *label;
    double total_ms;
    size_t num_lines;
    size_t total_runes;
    size_t rounds;
};

static auto print_report(const BenchResult &r) -> void {
    auto total_ops = static_cast<double>(r.num_lines * r.rounds);
    auto total_runes = static_cast<double>(r.total_runes * r.rounds);
    auto ns_per_line = r.total_ms * 1e6 / total_ops;
    auto ns_per_rune = r.total_ms * 1e6 / total_runes;
    auto lines_per_sec = total_ops / (r.total_ms / 1000.0);
    auto runes_per_sec = total_runes / (r.total_ms / 1000.0);

    std::printf("┌──────────────────────────────────────────────────────┐\n");
    std::printf("│ %-52s │\n", r.label);
    std::printf("├──────────────────────────┬───────────────────────────┤\n");
    std::printf("│ Lines                    │ %25zu │\n", r.num_lines);
    std::printf("│ Total runes              │ %25zu │\n", r.total_runes);
    std::printf("│ Rounds                   │ %25zu │\n", r.rounds);
    std::printf("├──────────────────────────┼───────────────────────────┤\n");
    std::printf("│ Total time               │ %22.2f ms │\n", r.total_ms);
    std::printf("│ Per line                 │ %22.1f ns │\n", ns_per_line);
    std::printf("│ Per rune                 │ %22.1f ns │\n", ns_per_rune);
    std::printf("│ Line throughput          │ %19.2f Kl/s │\n", lines_per_sec / 1e3);
    std::printf("│ Rune throughput          │ %19.2f Mr/s │\n", runes_per_sec / 1e6);
    std::printf("└──────────────────────────┴───────────────────────────┘\n\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// Correctness verification — compare old vs neo for one method
//
// OldCutFn: (const std::string &) -> std::vector<std::string>
// NeoCutFn: (const std::string &) -> std::vector<std::string>
// ─────────────────────────────────────────────────────────────────────────────
template <typename OldCutFn, typename NeoCutFn>
static auto verify_method(const char *method_name, const OldCutFn &old_fn, const NeoCutFn &neo_fn,
                          const std::vector<std::string> &lines) -> bool {
    std::printf("  %-8s: ", method_name);
    auto mismatches = size_t{0};
    for (auto i = size_t{0}; i < lines.size(); ++i) {
        auto old_words = old_fn(lines[i]);
        auto neo_words = neo_fn(lines[i]);
        if (old_words != neo_words) {
            ++mismatches;
            if (mismatches <= 3) {
                std::printf("\n    MISMATCH line %zu: \"%.*s\"\n", i,
                            static_cast<int>(std::min(lines[i].size(), size_t{60})), lines[i].data());
                std::printf("      Old (%zu):", old_words.size());
                for (auto j = size_t{0}; j < std::min(old_words.size(), size_t{15}); ++j) {
                    std::printf(" [%s]", old_words[j].c_str());
                }
                if (old_words.size() > 15) {
                    std::printf(" ...");
                }
                std::printf("\n");
                std::printf("      Neo (%zu):", neo_words.size());
                for (auto j = size_t{0}; j < std::min(neo_words.size(), size_t{15}); ++j) {
                    std::printf(" [%s]", neo_words[j].c_str());
                }
                if (neo_words.size() > 15) {
                    std::printf(" ...");
                }
                std::printf("\n    ");
            }
        }
    }
    if (mismatches == 0) {
        std::printf("✓ %zu lines identical\n", lines.size());
        return true;
    }
    std::printf("✗ %zu / %zu lines differ\n", mismatches, lines.size());
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Generic benchmark runner (Unicode-level, pre-decoded)
//
// CutFn: (const Unicode &) -> std::vector<WordRange>
// ─────────────────────────────────────────────────────────────────────────────
template <typename CutFn>
static auto bench_cut_unicode(const char *label, const CutFn &fn,
                              const std::vector<neo_cppjieba::Unicode> &all_unicodes, size_t total_runes, size_t rounds)
    -> BenchResult {
    auto n = all_unicodes.size();

    // Warm up
    for (auto i = size_t{0}; i < n; ++i) {
        auto result = fn(all_unicodes[i]);
        DoNotOptimize(result);
    }

    // Timed run
    auto t0 = Clock::now();
    for (auto round = size_t{0}; round < rounds; ++round) {
        for (auto i = size_t{0}; i < n; ++i) {
            auto result = fn(all_unicodes[i]);
            DoNotOptimize(result);
        }
    }
    auto t1 = Clock::now();

    return {label, Ms(t1 - t0).count(), n, total_runes, rounds};
}

// ─────────────────────────────────────────────────────────────────────────────
// Generic benchmark runner (string-level, full pipeline)
//
// CutFn: (const std::string &) -> std::vector<std::string>
// ─────────────────────────────────────────────────────────────────────────────
template <typename CutFn>
static auto bench_cut_string(const char *label, const CutFn &fn, const std::vector<std::string> &lines,
                             size_t total_runes, size_t rounds) -> BenchResult {
    auto n = lines.size();

    // Warm up
    for (auto i = size_t{0}; i < n; ++i) {
        auto result = fn(lines[i]);
        DoNotOptimize(result);
    }

    // Timed run
    auto t0 = Clock::now();
    for (auto round = size_t{0}; round < rounds; ++round) {
        for (auto i = size_t{0}; i < n; ++i) {
            auto result = fn(lines[i]);
            DoNotOptimize(result);
        }
    }
    auto t1 = Clock::now();

    return {label, Ms(t1 - t0).count(), n, total_runes, rounds};
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────
auto main(int argc, char *argv[]) -> int {
    auto dict_path = std::string(DICT_DIR) + "/jieba.dict.utf8";
    auto model_path = std::string(DICT_DIR) + "/hmm_model.utf8";
    auto user_dict_path = std::string(DICT_DIR) + "/user.dict.utf8";
    auto text_path = std::string(TEST_DATA_DIR) + "/weicheng.utf8";

    if (argc > 1) {
        dict_path = argv[1];
    }
    if (argc > 2) {
        model_path = argv[2];
    }
    if (argc > 3) {
        text_path = argv[3];
    }

    constexpr auto ROUNDS = size_t{50};

    std::printf("Dictionary : %s\n", dict_path.c_str());
    std::printf("HMM Model  : %s\n", model_path.c_str());
    std::printf("User Dict  : %s\n", user_dict_path.c_str());
    std::printf("Text file  : %s\n", text_path.c_str());
    std::printf("Rounds     : %zu\n\n", ROUNDS);

    // ── Load text ────────────────────────────────────────────────────────
    auto lines = load_lines(text_path);
    auto total_bytes = size_t{0};
    for (auto &l : lines) {
        total_bytes += l.size();
    }
    std::printf("Loaded %zu lines (%zu bytes UTF-8)\n\n", lines.size(), total_bytes);

    // ── Pre-decode all lines to Unicode (excluded from neo core timing) ──
    auto all_unicodes = std::vector<neo_cppjieba::Unicode>{};
    all_unicodes.reserve(lines.size());
    auto total_runes = size_t{0};
    for (auto &line : lines) {
        all_unicodes.push_back(neo_cppjieba::decode(line));
        total_runes += all_unicodes.back().size();
    }
    std::printf("Decoded %zu runes total\n\n", total_runes);

    // ── Construct Neo Jieba ──────────────────────────────────────────────
    std::printf("Loading Neo Jieba ...\n");
    auto t0 = Clock::now();
    auto jieba = neo_cppjieba::Jieba{dict_path, model_path, user_dict_path};
    auto t1 = Clock::now();
    std::printf("Neo Jieba loaded in %.2f ms\n\n", Ms(t1 - t0).count());

    // ── Construct Old Jieba ──────────────────────────────────────────────
    std::printf("Loading Old Jieba ...\n");
    t0 = Clock::now();
    auto old_jieba = cppjieba::Jieba{dict_path, model_path, user_dict_path};
    t1 = Clock::now();
    std::printf("Old Jieba loaded in %.2f ms\n\n", Ms(t1 - t0).count());

    // =====================================================================
    // Part 1: Correctness verification (Old == Neo for each method)
    // =====================================================================
    std::printf("═══════════════════════════════════════════════════════\n");
    std::printf("  Correctness: Old vs Neo\n");
    std::printf("═══════════════════════════════════════════════════════\n");

    auto all_ok = true;

    all_ok &= verify_method(
        "MIX",
        [&](const std::string &s) {
            auto w = std::vector<std::string>{};
            old_jieba.Cut(s, w);
            return w;
        },
        [&](const std::string &s) { return jieba.cut_strings(s, neo_cppjieba::CutMode::MIX); }, lines);

    all_ok &= verify_method(
        "MP",
        [&](const std::string &s) {
            auto w = std::vector<std::string>{};
            old_jieba.CutSmall(s, w, 99);
            return w;
        },
        [&](const std::string &s) { return jieba.cut_strings(s, neo_cppjieba::CutMode::MP); }, lines);

    all_ok &= verify_method(
        "HMM",
        [&](const std::string &s) {
            auto w = std::vector<std::string>{};
            old_jieba.CutHMM(s, w);
            return w;
        },
        [&](const std::string &s) { return jieba.cut_strings(s, neo_cppjieba::CutMode::HMM); }, lines);

    all_ok &= verify_method(
        "FULL",
        [&](const std::string &s) {
            auto w = std::vector<std::string>{};
            old_jieba.CutAll(s, w);
            return w;
        },
        [&](const std::string &s) { return jieba.cut_strings(s, neo_cppjieba::CutMode::FULL); }, lines);

    all_ok &= verify_method(
        "SEARCH",
        [&](const std::string &s) {
            auto w = std::vector<std::string>{};
            old_jieba.CutForSearch(s, w);
            return w;
        },
        [&](const std::string &s) { return jieba.cut_strings(s, neo_cppjieba::CutMode::SEARCH); }, lines);

    std::printf("\n");
    if (!all_ok) {
        std::printf("WARNING: Some methods differ. Benchmark proceeds anyway.\n\n");
    }

    // =====================================================================
    // Part 2: Neo 5-method comparison (Unicode-level, core algorithm)
    // =====================================================================
    std::printf("═══════════════════════════════════════════════════════\n");
    std::printf("  Neo: 5 methods comparison (Unicode-level, %zu rounds)\n", ROUNDS);
    std::printf("═══════════════════════════════════════════════════════\n\n");

    auto neo_mix = bench_cut_unicode(
        "Neo MIX  (MP + HMM)",
        [&](const neo_cppjieba::Unicode &u) { return jieba.cut_runes(u, neo_cppjieba::CutMode::MIX); }, all_unicodes,
        total_runes, ROUNDS);
    print_report(neo_mix);

    auto neo_mp = bench_cut_unicode(
        "Neo MP   (dictionary-only)",
        [&](const neo_cppjieba::Unicode &u) { return jieba.cut_runes(u, neo_cppjieba::CutMode::MP); }, all_unicodes,
        total_runes, ROUNDS);
    print_report(neo_mp);

    auto neo_hmm = bench_cut_unicode(
        "Neo HMM  (Viterbi only)",
        [&](const neo_cppjieba::Unicode &u) { return jieba.cut_runes(u, neo_cppjieba::CutMode::HMM); }, all_unicodes,
        total_runes, ROUNDS);
    print_report(neo_hmm);

    auto neo_full = bench_cut_unicode(
        "Neo FULL (all dictionary words)",
        [&](const neo_cppjieba::Unicode &u) { return jieba.cut_runes(u, neo_cppjieba::CutMode::FULL); }, all_unicodes,
        total_runes, ROUNDS);
    print_report(neo_full);

    auto neo_search = bench_cut_unicode(
        "Neo SEARCH (MIX + sub-word)",
        [&](const neo_cppjieba::Unicode &u) { return jieba.cut_runes(u, neo_cppjieba::CutMode::SEARCH); }, all_unicodes,
        total_runes, ROUNDS);
    print_report(neo_search);

    // ── Neo methods summary table ────────────────────────────────────────
    {
        struct Entry {
            const char *name;
            double ms;
        };
        auto entries = std::array<Entry, 5>{{
            {"MIX", neo_mix.total_ms},
            {"MP", neo_mp.total_ms},
            {"HMM", neo_hmm.total_ms},
            {"FULL", neo_full.total_ms},
            {"SEARCH", neo_search.total_ms},
        }};
        auto *fastest = std::min_element(entries.begin(), entries.end(), [](auto &a, auto &b) { return a.ms < b.ms; });

        std::printf("──────────────────────────────────────────────────────────────\n");
        std::printf("  Neo methods (ratio = time / fastest)\n");
        std::printf("──────────────────────────────────────────────────────────────\n");
        std::printf("  %-8s  %10s  %8s  %s\n", "Method", "Time (ms)", "Ratio", "");
        std::printf("  ────────  ──────────  ────────  ─────────────────────────\n");
        for (auto &e : entries) {
            auto ratio = e.ms / fastest->ms;
            auto rps = static_cast<double>(total_runes * ROUNDS) / (e.ms / 1000.0);
            const auto *m = (e.ms == fastest->ms) ? " ◀ fastest" : "";
            std::printf("  %-8s  %10.2f  %7.2fx  %6.2f Mr/s%s\n", e.name, e.ms, ratio, rps / 1e6, m);
        }
        std::printf("──────────────────────────────────────────────────────────────\n\n");
    }

    // =====================================================================
    // Part 3: Old Jieba vs Neo Jieba (string-level, full pipeline)
    // =====================================================================
    std::printf("═══════════════════════════════════════════════════════\n");
    std::printf("  Old Jieba vs Neo Jieba (string → cut → strings, %zu rounds)\n", ROUNDS);
    std::printf("═══════════════════════════════════════════════════════\n\n");

    using CM = neo_cppjieba::CutMode;

    // ── MIX ──
    auto old_mix_r = bench_cut_string(
        "Old Jieba.Cut  (MIX)",
        [&](const std::string &s) {
            auto w = std::vector<std::string>{};
            old_jieba.Cut(s, w);
            return w;
        },
        lines, total_runes, ROUNDS);
    print_report(old_mix_r);

    auto neo_mix_str = bench_cut_string(
        "Neo Jieba.cut_strings(MIX)", [&](const std::string &s) { return jieba.cut_strings(s, CM::MIX); }, lines,
        total_runes, ROUNDS);
    print_report(neo_mix_str);

    // ── MP ──
    auto old_mp_r = bench_cut_string(
        "Old Jieba.CutSmall  (MP)",
        [&](const std::string &s) {
            auto w = std::vector<std::string>{};
            old_jieba.CutSmall(s, w, 99);
            return w;
        },
        lines, total_runes, ROUNDS);
    print_report(old_mp_r);

    auto neo_mp_str = bench_cut_string(
        "Neo Jieba.cut_strings(MP)", [&](const std::string &s) { return jieba.cut_strings(s, CM::MP); }, lines,
        total_runes, ROUNDS);
    print_report(neo_mp_str);

    // ── HMM ──
    auto old_hmm_r = bench_cut_string(
        "Old Jieba.CutHMM  (HMM)",
        [&](const std::string &s) {
            auto w = std::vector<std::string>{};
            old_jieba.CutHMM(s, w);
            return w;
        },
        lines, total_runes, ROUNDS);
    print_report(old_hmm_r);

    auto neo_hmm_str = bench_cut_string(
        "Neo Jieba.cut_strings(HMM)", [&](const std::string &s) { return jieba.cut_strings(s, CM::HMM); }, lines,
        total_runes, ROUNDS);
    print_report(neo_hmm_str);

    // ── FULL ──
    auto old_full_r = bench_cut_string(
        "Old Jieba.CutAll  (FULL)",
        [&](const std::string &s) {
            auto w = std::vector<std::string>{};
            old_jieba.CutAll(s, w);
            return w;
        },
        lines, total_runes, ROUNDS);
    print_report(old_full_r);

    auto neo_full_str = bench_cut_string(
        "Neo Jieba.cut_strings(FULL)", [&](const std::string &s) { return jieba.cut_strings(s, CM::FULL); }, lines,
        total_runes, ROUNDS);
    print_report(neo_full_str);

    // ── SEARCH ──
    auto old_query_r = bench_cut_string(
        "Old Jieba.CutForSearch  (SEARCH)",
        [&](const std::string &s) {
            auto w = std::vector<std::string>{};
            old_jieba.CutForSearch(s, w);
            return w;
        },
        lines, total_runes, ROUNDS);
    print_report(old_query_r);

    auto neo_search_str = bench_cut_string(
        "Neo Jieba.cut_strings(SEARCH)", [&](const std::string &s) { return jieba.cut_strings(s, CM::SEARCH); }, lines,
        total_runes, ROUNDS);
    print_report(neo_search_str);

    // ── Old vs Neo summary table ─────────────────────────────────────────
    {
        struct Pair {
            const char *name;
            double old_ms;
            double neo_ms;
        };
        auto pairs = std::array<Pair, 5>{{
            {"MIX", old_mix_r.total_ms, neo_mix_str.total_ms},
            {"MP", old_mp_r.total_ms, neo_mp_str.total_ms},
            {"HMM", old_hmm_r.total_ms, neo_hmm_str.total_ms},
            {"FULL", old_full_r.total_ms, neo_full_str.total_ms},
            {"SEARCH", old_query_r.total_ms, neo_search_str.total_ms},
        }};

        std::printf("══════════════════════════════════════════════════════════════\n");
        std::printf("  Old Jieba vs Neo Jieba  (speedup = Old time / Neo time)\n");
        std::printf("══════════════════════════════════════════════════════════════\n");
        std::printf("  %-8s  %12s  %12s  %8s  %s\n", "Method", "Old (ms)", "Neo (ms)", "Speedup", "");
        std::printf("  ────────  ────────────  ────────────  ────────  ──────────\n");
        for (auto &p : pairs) {
            auto speedup = p.old_ms / p.neo_ms;
            const auto *tag = p.neo_ms < p.old_ms ? "Neo ✓" : "Old ✓";
            std::printf("  %-8s  %12.2f  %12.2f  %7.2fx  %s\n", p.name, p.old_ms, p.neo_ms, speedup, tag);
        }
        std::printf("══════════════════════════════════════════════════════════════\n");
    }

    return 0;
}
