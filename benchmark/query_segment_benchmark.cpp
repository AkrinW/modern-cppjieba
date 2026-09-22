/// Benchmark: Neo QuerySegment baseline re-query vs two DAG-reuse designs
///
/// Loads a Chinese text file, pre-decodes every line to Unicode, verifies that
/// the baseline implementation (`cut_with_requery`) and the two DAG-based
/// implementations (`cut_with_mix_dag`, `cut_with_inline_dag`) produce
/// identical output, then compares their throughput.

#include "neo/DictTrie.hpp"
#include "neo/HMModel.hpp"
#include "neo/QuerySegment.hpp"
#include "neo/Unicode.hpp"

#include "BenchmarkUtils.hpp"
#include "QuerySegmentCompare.hpp"
#include "test_paths.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
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

template <typename CutFn>
static auto bench(const char *label, const CutFn &fn, const std::vector<neo_cppjieba::Unicode> &all_unicodes,
                  size_t total_runes, size_t rounds) -> BenchResult {
    auto n = all_unicodes.size();

    for (auto i = size_t{0}; i < n; ++i) {
        auto result = fn(all_unicodes[i]);
        DoNotOptimize(result);
    }

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

} // namespace

auto main(int argc, char *argv[]) -> int {
    namespace neo = neo_cppjieba;

    auto dict_path = std::string(DICT_DIR) + "/jieba.dict.utf8";
    auto model_path = std::string(DICT_DIR) + "/hmm_model.utf8";
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
    std::printf("Text file  : %s\n", text_path.c_str());
    std::printf("Rounds     : %zu\n\n", ROUNDS);

    auto lines = load_lines(text_path);
    auto total_bytes = size_t{0};
    for (auto &line : lines) {
        total_bytes += line.size();
    }
    std::printf("Loaded %zu lines (%zu bytes UTF-8)\n", lines.size(), total_bytes);

    auto all_unicodes = std::vector<neo::Unicode>{};
    all_unicodes.reserve(lines.size());
    auto total_runes = size_t{0};
    for (auto &line : lines) {
        all_unicodes.push_back(neo::decode(line));
        total_runes += all_unicodes.back().size();
    }
    std::printf("Decoded %zu runes total\n\n", total_runes);

    std::printf("Loading Neo dictionary/model ...\n");
    auto t0 = Clock::now();
    auto dict = neo::DictTrie{dict_path};
    auto model = neo::HMModel{model_path};
    auto t1 = Clock::now();
    std::printf("Loaded in %.2f ms\n\n", Ms(t1 - t0).count());

    std::printf("Verifying baseline and DAG-reuse outputs ...\n");
    auto mismatch = size_t{0};
    for (auto i = size_t{0}; i < all_unicodes.size(); ++i) {
        auto baseline_result = neo::test::query_cut_requery(dict, model, all_unicodes[i]);
        auto buffered_result = neo::test::query_cut_buffered_dag(dict, model, all_unicodes[i]);
        auto inline_result = neo::QuerySegment<>::cut(dict, model, all_unicodes[i]);
        if (baseline_result != buffered_result || baseline_result != inline_result) {
            ++mismatch;
            if (mismatch <= 3) {
                auto input = neo::encode(all_unicodes[i]);
                std::printf("  MISMATCH line %zu: \"%.*s\"\n", i, static_cast<int>(std::min(input.size(), size_t{80})),
                            input.data());
            }
        }
    }
    if (mismatch != 0) {
        std::printf("Verification failed: %zu lines differ\n", mismatch);
        return 1;
    }
    std::printf("Verified: %zu lines identical\n\n", all_unicodes.size());

    auto baseline = bench(
        "QuerySegment baseline (Mix + dict.find 2/3-gram)",
        [&](const neo::Unicode &runes) { return neo::test::query_cut_requery(dict, model, runes); }, all_unicodes,
        total_runes, ROUNDS);
    print_report(baseline);

    auto buffered = bench(
        "QuerySegment DAG reuse (buffered Mix DAG + edge lookup)",
        [&](const neo::Unicode &runes) { return neo::test::query_cut_buffered_dag(dict, model, runes); }, all_unicodes,
        total_runes, ROUNDS);
    print_report(buffered);

    auto inline_reuse = bench(
        "QuerySegment DAG reuse (inline segment consume)",
        [&](const neo::Unicode &runes) { return neo::QuerySegment<>::cut(dict, model, runes); }, all_unicodes,
        total_runes, ROUNDS);
    print_report(inline_reuse);

    std::printf("══════════════════════════════════════════════════════\n");
    std::printf("  COMPARISON  (speedup = baseline time / DAG reuse)\n");
    std::printf("══════════════════════════════════════════════════════\n");
    auto buffered_speedup = baseline.total_ms / buffered.total_ms;
    auto inline_speedup = baseline.total_ms / inline_reuse.total_ms;
    const auto *buffered_tag = buffered.total_ms < baseline.total_ms ? "(buffered faster)" : "(baseline faster)";
    const auto *inline_tag = inline_reuse.total_ms < baseline.total_ms ? "(inline faster)" : "(baseline faster)";
    std::printf("  Buffered DAG : %5.2fx  %s\n", buffered_speedup, buffered_tag);
    std::printf("  Inline DAG   : %5.2fx  %s\n", inline_speedup, inline_tag);
    std::printf("  Baseline     : %8.2f ms\n", baseline.total_ms);
    std::printf("  Buffered DAG : %8.2f ms\n", buffered.total_ms);
    std::printf("  Inline DAG   : %8.2f ms\n", inline_reuse.total_ms);
    std::printf("══════════════════════════════════════════════════════\n");

    return 0;
}
