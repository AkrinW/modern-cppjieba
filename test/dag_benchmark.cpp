/// Benchmark: Old DAG construction vs Neo DAG construction
///
/// Loads a Chinese text file, decodes each line to runes, then compares
/// the DAG-building throughput of the old cppjieba::Trie::Find(begin, end, dags)
/// versus the new neo_cppjieba::Trie::find_dag(runes).
///
/// Both use the same underlying dictionary (jieba.dict.utf8).

// ── Old headers ──
#include "cppjieba/DictTrie.hpp"
#include "cppjieba/Unicode.hpp"

// ── Neo headers ──
#include "neo/DictTrie.hpp"

#include "BenchmarkUtils.hpp"
#include "test_paths.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <vector>

#include <sys/mman.h>
#include <sys/stat.h>

// ─────────────────────────────────────────────────────────────────────────────
// Anti-optimization barrier
// ─────────────────────────────────────────────────────────────────────────────
namespace {

struct BenchResult {
    double total_ms;
    size_t num_lines;
    size_t total_runes;
    size_t rounds;
};

static auto print_report(const char *label, const BenchResult &r) -> void {
    auto total_ops = static_cast<double>(r.num_lines * r.rounds);
    auto total_runes = static_cast<double>(r.total_runes * r.rounds);
    auto ns_per_line = r.total_ms * 1e6 / total_ops;
    auto ns_per_rune = r.total_ms * 1e6 / total_runes;
    auto lines_per_sec = total_ops / (r.total_ms / 1000.0);
    auto runes_per_sec = total_runes / (r.total_ms / 1000.0);

    std::printf("┌──────────────────────────────────────────────────────┐\n");
    std::printf("│ %-52s │\n", label);
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
// Benchmark: Old DAG construction (cppjieba::Trie::Find → vector<Dag>)
// ─────────────────────────────────────────────────────────────────────────────
static auto bench_old_dag(const std::string &dict_path, const std::vector<std::string> &lines, size_t rounds)
    -> BenchResult {
    namespace old = cppjieba;

    auto dict_trie = old::DictTrie{dict_path};

    // Pre-decode all lines to RuneStrArray (excluded from timing)
    auto n = lines.size();
    auto rune_arrays = std::vector<old::RuneStrArray>(n);
    auto total_runes = size_t{0};
    for (auto i = size_t{0}; i < n; ++i) {
        old::DecodeUTF8RunesInString(lines[i], rune_arrays[i]);
        total_runes += rune_arrays[i].size();
    }

    // Warm up
    {
        auto dags = std::vector<old::Dag>{};
        for (auto i = size_t{0}; i < n; ++i) {
            auto &ra = rune_arrays[i];
            dict_trie.Find(ra.begin(), ra.end(), dags);
            DoNotOptimize(dags);
        }
    }

    // Timed run
    auto t0 = Clock::now();
    for (auto round = size_t{0}; round < rounds; ++round) {
        auto dags = std::vector<old::Dag>{};
        for (auto i = size_t{0}; i < n; ++i) {
            auto &ra = rune_arrays[i];
            dict_trie.Find(ra.begin(), ra.end(), dags);
            DoNotOptimize(dags);
        }
    }
    auto t1 = Clock::now();

    return {Ms(t1 - t0).count(), n, total_runes, rounds};
}

// ─────────────────────────────────────────────────────────────────────────────
// Benchmark: Neo DAG construction (neo_cppjieba::Trie::find_dag → Dag)
// ─────────────────────────────────────────────────────────────────────────────
static auto bench_neo_dag(const std::string &dict_path, const std::vector<std::string> &lines, size_t rounds)
    -> BenchResult {
    namespace neo = neo_cppjieba;

    auto dict_trie = neo::DictTrie{dict_path};
    const auto &trie = dict_trie.trie();

    // Pre-decode all lines to Unicode (excluded from timing)
    auto n = lines.size();
    auto rune_vecs = std::vector<neo::Unicode>(n);
    auto total_runes = size_t{0};
    for (auto i = size_t{0}; i < n; ++i) {
        rune_vecs[i] = neo::decode(lines[i]);
        total_runes += rune_vecs[i].size();
    }

    // Warm up
    {
        for (auto i = size_t{0}; i < n; ++i) {
            auto dag = trie.find_dag(rune_vecs[i]);
            DoNotOptimize(dag);
        }
    }

    // Timed run
    auto t0 = Clock::now();
    for (auto round = size_t{0}; round < rounds; ++round) {
        for (auto i = size_t{0}; i < n; ++i) {
            auto dag = trie.find_dag(rune_vecs[i]);
            DoNotOptimize(dag);
        }
    }
    auto t1 = Clock::now();

    return {Ms(t1 - t0).count(), n, total_runes, rounds};
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────
auto main(int argc, char *argv[]) -> int {
    auto dict_path = std::string(DICT_DIR) + "/jieba.dict.utf8";
    auto text_path = std::string(TEST_DATA_DIR) + "/weicheng.utf8";

    if (argc > 1) {
        dict_path = argv[1];
    }
    if (argc > 2) {
        text_path = argv[2];
    }

    constexpr auto ROUNDS = size_t{50};

    std::printf("Dictionary : %s\n", dict_path.c_str());
    std::printf("Text file  : %s\n", text_path.c_str());
    std::printf("Rounds     : %zu\n\n", ROUNDS);

    auto lines = load_lines(text_path);
    auto total_chars = size_t{0};
    for (auto &l : lines) {
        total_chars += l.size();
    }
    std::printf("Loaded %zu lines (%zu bytes UTF-8)\n\n", lines.size(), total_chars);

    // ── Old DAG ──────────────────────────────────────────────────────────
    std::printf("Benchmarking Old DAG construction ...\n");
    auto old_result = bench_old_dag(dict_path, lines, ROUNDS);
    print_report("Old DAG (vector<Dag> + LocalVector<pair>)", old_result);

    // ── Neo DAG ──────────────────────────────────────────────────────────
    std::printf("Benchmarking Neo DAG construction ...\n");
    auto neo_result = bench_neo_dag(dict_path, lines, ROUNDS);
    print_report("Neo DAG (flat CSR: offsets + edges)", neo_result);

    // ── Comparison ───────────────────────────────────────────────────────
    std::printf("══════════════════════════════════════════════════════\n");
    std::printf("  COMPARISON  (speedup = Old time / Neo time)\n");
    std::printf("══════════════════════════════════════════════════════\n");
    auto r = old_result.total_ms / neo_result.total_ms;
    const auto *tag = neo_result.total_ms < old_result.total_ms ? "(Neo faster)" : "(Old faster)";
    std::printf("  DAG construction : %5.2fx  %s\n", r, tag);
    std::printf("  Old              : %8.2f ms\n", old_result.total_ms);
    std::printf("  Neo              : %8.2f ms\n", neo_result.total_ms);
    std::printf("══════════════════════════════════════════════════════\n");

    return 0;
}
