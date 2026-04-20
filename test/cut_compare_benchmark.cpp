#include "BenchmarkUtils.hpp"
#include "RustJiebaCapi.hpp"
#include "cppjieba/Jieba.hpp"
#include "neo/Jieba.hpp"
#include "test_paths.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
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

struct SharedMethodResult {
    const char *method_name;
    bool matched;
    BenchResult old_result;
    BenchResult neo_result;
    BenchResult rust_result;
};

struct PairMethodResult {
    const char *method_name;
    bool matched;
    BenchResult old_result;
    BenchResult neo_result;
};

auto print_words_preview(const char *label, const std::vector<std::string> &words) -> void {
    constexpr auto kMaxWordsToPrint = size_t{20};
    std::printf("%s", label);
    for (auto i = size_t{0}; i < std::min(words.size(), kMaxWordsToPrint); ++i) {
        std::printf(" [%s]", words[i].c_str());
    }
    if (words.size() > kMaxWordsToPrint) {
        std::printf(" ... (%zu words)", words.size());
    }
    std::printf("\n");
}

auto print_report(const BenchResult &r) -> void {
    const auto total_ops = static_cast<double>(r.num_lines * r.rounds);
    const auto total_runes = static_cast<double>(r.total_runes * r.rounds);
    const auto ns_per_line = r.total_ms * 1e6 / total_ops;
    const auto ns_per_rune = r.total_ms * 1e6 / total_runes;
    const auto lines_per_sec = total_ops / (r.total_ms / 1000.0);
    const auto runes_per_sec = total_runes / (r.total_ms / 1000.0);

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

template <typename CutFn>
auto bench_cut(const char *label, const CutFn &fn, const std::vector<std::string> &lines, size_t total_runes, size_t rounds)
    -> BenchResult {
    for (const auto &line : lines) {
        auto words = fn(line);
        DoNotOptimize(words);
    }

    const auto t0 = Clock::now();
    for (auto round = size_t{0}; round < rounds; ++round) {
        for (const auto &line : lines) {
            auto words = fn(line);
            DoNotOptimize(words);
        }
    }
    const auto t1 = Clock::now();

    return {label, Ms(t1 - t0).count(), lines.size(), total_runes, rounds};
}

template <typename OldCutFn, typename NeoCutFn, typename RustCutFn>
auto verify_three_way(const char *method_name, const OldCutFn &old_fn, const NeoCutFn &neo_fn, const RustCutFn &rust_fn,
                      const std::vector<std::string> &lines) -> bool {
    std::printf("Correctness [%s]\n", method_name);
    auto mismatches = size_t{0};
    for (auto i = size_t{0}; i < lines.size(); ++i) {
        const auto old_words = old_fn(lines[i]);
        const auto neo_words = neo_fn(lines[i]);
        const auto rust_words = rust_fn(lines[i]);
        if (!(old_words == neo_words && old_words == rust_words)) {
            ++mismatches;
            if (mismatches <= 3) {
                std::printf("  Mismatch line %zu: \"%.*s\"\n", i,
                            static_cast<int>(std::min(lines[i].size(), size_t{80})), lines[i].data());
                print_words_preview("    old :", old_words);
                print_words_preview("    neo :", neo_words);
                print_words_preview("    rust:", rust_words);
            }
        }
    }

    if (mismatches == 0) {
        std::printf("  old / neo / rust matched on %zu lines.\n\n", lines.size());
        return true;
    }

    std::printf("  %zu / %zu lines differ. Benchmark proceeds anyway.\n\n", mismatches, lines.size());
    return false;
}

template <typename OldCutFn, typename NeoCutFn>
auto verify_old_vs_neo(const char *method_name, const OldCutFn &old_fn, const NeoCutFn &neo_fn,
                       const std::vector<std::string> &lines) -> bool {
    std::printf("Correctness [%s]\n", method_name);
    auto mismatches = size_t{0};
    for (auto i = size_t{0}; i < lines.size(); ++i) {
        const auto old_words = old_fn(lines[i]);
        const auto neo_words = neo_fn(lines[i]);
        if (old_words != neo_words) {
            ++mismatches;
            if (mismatches <= 3) {
                std::printf("  Mismatch line %zu: \"%.*s\"\n", i,
                            static_cast<int>(std::min(lines[i].size(), size_t{80})), lines[i].data());
                print_words_preview("    old:", old_words);
                print_words_preview("    neo:", neo_words);
            }
        }
    }

    if (mismatches == 0) {
        std::printf("  old / neo matched on %zu lines.\n\n", lines.size());
        return true;
    }

    std::printf("  %zu / %zu lines differ. Benchmark proceeds anyway.\n\n", mismatches, lines.size());
    return false;
}

auto print_three_way_summary(const SharedMethodResult &result) -> void {
    struct SummaryRow {
        const char *name;
        double ms;
    };

    auto rows = std::array<SummaryRow, 3>{{
        {"old cppjieba", result.old_result.total_ms},
        {"neo cppjieba", result.neo_result.total_ms},
        {"rust jieba", result.rust_result.total_ms},
    }};
    const auto fastest = std::min_element(rows.begin(), rows.end(), [](const auto &lhs, const auto &rhs) {
        return lhs.ms < rhs.ms;
    });

    std::printf("Summary [%s] (ratio = time / fastest)\n", result.method_name);
    std::printf("  %-14s  %12s  %8s  %s\n", "Implementation", "Time (ms)", "Ratio", "");
    std::printf("  ──────────────  ────────────  ────────  ─────────────\n");
    for (const auto &row : rows) {
        const auto ratio = row.ms / fastest->ms;
        const auto marker = (row.ms == fastest->ms) ? "◀ fastest" : "";
        std::printf("  %-14s  %12.2f  %7.2fx  %s\n", row.name, row.ms, ratio, marker);
    }
    std::printf("\n");
}

auto print_pair_summary(const PairMethodResult &result) -> void {
    const auto speedup = result.old_result.total_ms / result.neo_result.total_ms;
    const auto faster = result.neo_result.total_ms < result.old_result.total_ms ? "neo cppjieba" : "old cppjieba";

    std::printf("Summary [%s]\n", result.method_name);
    std::printf("  %-14s  %12.2f ms\n", "old cppjieba", result.old_result.total_ms);
    std::printf("  %-14s  %12.2f ms\n", "neo cppjieba", result.neo_result.total_ms);
    std::printf("  Old / Neo speedup: %.2fx (%s faster)\n\n", speedup, faster);
}

template <typename OldCutFn, typename NeoCutFn, typename RustCutFn>
auto run_shared_method(const char *method_name, const char *old_label, const char *neo_label, const char *rust_label,
                       const OldCutFn &old_fn, const NeoCutFn &neo_fn, const RustCutFn &rust_fn,
                       const std::vector<std::string> &lines, size_t total_runes, size_t rounds) -> SharedMethodResult {
    std::printf("═══════════════════════════════════════════════════════\n");
    std::printf("  %s\n", method_name);
    std::printf("═══════════════════════════════════════════════════════\n\n");

    const auto matched = verify_three_way(method_name, old_fn, neo_fn, rust_fn, lines);

    const auto old_result = bench_cut(old_label, old_fn, lines, total_runes, rounds);
    print_report(old_result);

    const auto neo_result = bench_cut(neo_label, neo_fn, lines, total_runes, rounds);
    print_report(neo_result);

    const auto rust_result = bench_cut(rust_label, rust_fn, lines, total_runes, rounds);
    print_report(rust_result);

    const auto result = SharedMethodResult{method_name, matched, old_result, neo_result, rust_result};
    print_three_way_summary(result);
    return result;
}

template <typename OldCutFn, typename NeoCutFn>
auto run_pair_method(const char *method_name, const char *old_label, const char *neo_label, const OldCutFn &old_fn,
                     const NeoCutFn &neo_fn, const std::vector<std::string> &lines, size_t total_runes, size_t rounds)
    -> PairMethodResult {
    std::printf("═══════════════════════════════════════════════════════\n");
    std::printf("  %s\n", method_name);
    std::printf("═══════════════════════════════════════════════════════\n\n");

    const auto matched = verify_old_vs_neo(method_name, old_fn, neo_fn, lines);

    const auto old_result = bench_cut(old_label, old_fn, lines, total_runes, rounds);
    print_report(old_result);

    const auto neo_result = bench_cut(neo_label, neo_fn, lines, total_runes, rounds);
    print_report(neo_result);

    const auto result = PairMethodResult{method_name, matched, old_result, neo_result};
    print_pair_summary(result);
    return result;
}

auto print_shared_matrix(const std::array<SharedMethodResult, 4> &results) -> void {
    std::printf("══════════════════════════════════════════════════════════════════════════════════\n");
    std::printf("  Shared methods summary\n");
    std::printf("══════════════════════════════════════════════════════════════════════════════════\n");
    std::printf("  %-10s  %12s  %12s  %12s  %-12s  %9s  %9s\n", "Method", "Old (ms)", "Neo (ms)", "Rust (ms)", "Fastest",
                "Old/Neo", "Old/Rust");
    std::printf("  ──────────  ────────────  ────────────  ────────────  ────────────  ─────────  ─────────\n");
    for (const auto &result : results) {
        struct WinnerRow {
            const char *name;
            double ms;
        };
        auto rows = std::array<WinnerRow, 3>{{
            {"old", result.old_result.total_ms},
            {"neo", result.neo_result.total_ms},
            {"rust", result.rust_result.total_ms},
        }};
        const auto fastest = std::min_element(rows.begin(), rows.end(), [](const auto &lhs, const auto &rhs) {
            return lhs.ms < rhs.ms;
        });
        std::printf("  %-10s  %12.2f  %12.2f  %12.2f  %-12s  %8.2fx  %8.2fx\n", result.method_name,
                    result.old_result.total_ms, result.neo_result.total_ms, result.rust_result.total_ms, fastest->name,
                    result.old_result.total_ms / result.neo_result.total_ms,
                    result.old_result.total_ms / result.rust_result.total_ms);
    }
    std::printf("══════════════════════════════════════════════════════════════════════════════════\n\n");
}

} // namespace

auto main(int argc, char *argv[]) -> int {
    auto dict_path = std::string(DICT_DIR) + "/jieba.dict.utf8";
    auto model_path = std::string(DICT_DIR) + "/hmm_model.utf8";
    auto user_dict_path = std::string(DICT_DIR) + "/user.dict.utf8";
    auto text_path = std::string(TEST_DATA_DIR) + "/weicheng.utf8";
    auto rounds = size_t{50};

    if (argc > 1) {
        dict_path = argv[1];
    }
    if (argc > 2) {
        model_path = argv[2];
    }
    if (argc > 3) {
        user_dict_path = argv[3];
    }
    if (argc > 4) {
        text_path = argv[4];
    }
    if (argc > 5) {
        rounds = static_cast<size_t>(std::strtoull(argv[5], nullptr, 10));
    }

    std::printf("Dictionary : %s\n", dict_path.c_str());
    std::printf("HMM Model  : %s\n", model_path.c_str());
    std::printf("User Dict  : %s\n", user_dict_path.c_str());
    std::printf("Text file  : %s\n", text_path.c_str());
    std::printf("Rounds     : %zu\n\n", rounds);

    const auto lines = load_lines(text_path);
    if (lines.empty()) {
        std::fprintf(stderr, "No input lines loaded from %s\n", text_path.c_str());
        return 1;
    }

    auto total_bytes = size_t{0};
    auto total_runes = size_t{0};
    for (const auto &line : lines) {
        total_bytes += line.size();
        total_runes += neo_cppjieba::decode(line).size();
    }
    std::printf("Loaded %zu lines (%zu bytes UTF-8, %zu runes)\n\n", lines.size(), total_bytes, total_runes);

    std::printf("Loading old cppjieba ...\n");
    auto t0 = Clock::now();
    auto old_jieba = cppjieba::Jieba{dict_path, model_path, user_dict_path};
    auto t1 = Clock::now();
    std::printf("Old cppjieba loaded in %.2f ms\n", Ms(t1 - t0).count());

    std::printf("Loading neo cppjieba ...\n");
    t0 = Clock::now();
    auto neo_jieba = neo_cppjieba::Jieba{dict_path, model_path, user_dict_path};
    t1 = Clock::now();
    std::printf("Neo cppjieba loaded in %.2f ms\n", Ms(t1 - t0).count());

    std::printf("Loading rust jieba ...\n");
    t0 = Clock::now();
    auto rust_jieba = RustJieba{dict_path, user_dict_path};
    t1 = Clock::now();
    std::printf("Rust jieba loaded in %.2f ms\n\n", Ms(t1 - t0).count());

    const auto mix_old = [&](const std::string &s) {
        auto words = std::vector<std::string>{};
        old_jieba.Cut(s, words, true);
        return words;
    };
    const auto mix_neo = [&](const std::string &s) { return neo_jieba.cut<neo_cppjieba::CutMethod::MIX>(s); };
    const auto mix_rust = [&](const std::string &s) { return rust_jieba.cut(s, true); };

    const auto mp_old = [&](const std::string &s) {
        auto words = std::vector<std::string>{};
        old_jieba.Cut(s, words, false);
        return words;
    };
    const auto mp_neo = [&](const std::string &s) { return neo_jieba.cut<neo_cppjieba::CutMethod::MIX, false>(s); };
    const auto mp_rust = [&](const std::string &s) { return rust_jieba.cut(s, false); };

    const auto full_old = [&](const std::string &s) {
        auto words = std::vector<std::string>{};
        old_jieba.CutAll(s, words);
        return words;
    };
    const auto full_neo = [&](const std::string &s) { return neo_jieba.cut<neo_cppjieba::CutMethod::FULL>(s); };
    const auto full_rust = [&](const std::string &s) { return rust_jieba.cut_all(s); };

    const auto search_old = [&](const std::string &s) {
        auto words = std::vector<std::string>{};
        old_jieba.CutForSearch(s, words, true);
        return words;
    };
    const auto search_neo = [&](const std::string &s) { return neo_jieba.cut<neo_cppjieba::CutMethod::SEARCH>(s); };
    const auto search_rust = [&](const std::string &s) { return rust_jieba.cut_for_search(s, true); };

    const auto hmm_only_old = [&](const std::string &s) {
        auto words = std::vector<std::string>{};
        old_jieba.CutHMM(s, words);
        return words;
    };
    const auto hmm_only_neo = [&](const std::string &s) { return neo_jieba.cut<neo_cppjieba::CutMethod::HMM>(s); };

    std::printf("═══════════════════════════════════════════════════════\n");
    std::printf("  Three-way benchmark: old cppjieba vs neo cppjieba vs rust jieba\n");
    std::printf("═══════════════════════════════════════════════════════\n\n");
    std::printf("Shared methods: MIX, MP(no HMM), FULL, SEARCH\n");
    std::printf("Rust jieba public API has no standalone HMM-only method, so HMM-only stays old/neo only.\n\n");

    const auto mix_result = run_shared_method("MIX", "Old cppjieba Cut()", "Neo cppjieba cut<MIX>()",
                                              "Rust jieba cut(hmm=true)", mix_old, mix_neo, mix_rust, lines,
                                              total_runes, rounds);

    const auto mp_result = run_shared_method("MP (no HMM)", "Old cppjieba Cut(hmm=false)",
                                             "Neo cppjieba cut<MIX,false>()", "Rust jieba cut(hmm=false)", mp_old,
                                             mp_neo, mp_rust, lines, total_runes, rounds);

    const auto full_result = run_shared_method("FULL", "Old cppjieba CutAll()", "Neo cppjieba cut<FULL>()",
                                               "Rust jieba cut_all()", full_old, full_neo, full_rust, lines,
                                               total_runes, rounds);

    const auto search_result =
        run_shared_method("SEARCH", "Old cppjieba CutForSearch()", "Neo cppjieba cut<SEARCH>()",
                          "Rust jieba cut_for_search()", search_old, search_neo, search_rust, lines, total_runes,
                          rounds);

    print_shared_matrix(std::array<SharedMethodResult, 4>{{mix_result, mp_result, full_result, search_result}});

    std::printf("═══════════════════════════════════════════════════════\n");
    std::printf("  Appendix: methods without rust counterpart\n");
    std::printf("═══════════════════════════════════════════════════════\n\n");

    const auto hmm_result = run_pair_method("HMM-only", "Old cppjieba CutHMM()", "Neo cppjieba cut<HMM>()",
                                            hmm_only_old, hmm_only_neo, lines, total_runes, rounds);

    if (!mix_result.matched || !mp_result.matched || !full_result.matched || !search_result.matched || !hmm_result.matched) {
        std::printf("Note: some correctness checks differ. Review mismatch samples before reading the timing as apples-to-apples.\n");
    }

    return 0;
}
