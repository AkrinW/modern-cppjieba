/// Benchmark: Old Trie vs Neo Trie
///
/// Reads jieba.dict.utf8, builds both tries, then compares:
///   1. Build time
///   2. Query throughput (all-hit sequential scan + random-order scan + miss queries)
///
/// Compile:
///   cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DCPPJIEBA_BUILD_BENCHMARKS=ON
///   cmake --build build-bench --target trie_benchmark
///
/// Run:
///   ./build-bench/benchmark/trie_benchmark [path/to/jieba.dict.utf8]

// ── Old Trie headers (defines cppjieba::Rune as uint32_t, cppjieba::Unicode as LocalVector<Rune>) ──
#include "cppjieba/Trie.hpp"
#include "cppjieba/Unicode.hpp"

// ── Neo Trie header (neo_cppjieba namespace — no conflict with cppjieba) ──
#include "neo/detail/Trie.hpp"

#include "BenchmarkUtils.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <numeric>
#include <random>
#include <string>
#include <unistd.h>
#include <vector>

#include <sys/mman.h>
#include <sys/stat.h>

namespace {

struct BenchResult {
    double build_ms;
    double seq_query_ms;
    double rand_query_ms;
    double miss_query_ms;
    size_t num_entries;
    size_t query_rounds;
};

static auto print_report(const char *label, const BenchResult &r) -> void {
    auto total_queries = static_cast<double>(r.num_entries * r.query_rounds);
    auto seq_qps = total_queries / (r.seq_query_ms / 1000.0);
    auto rand_qps = total_queries / (r.rand_query_ms / 1000.0);
    auto miss_qps = total_queries / (r.miss_query_ms / 1000.0);
    auto seq_ns = r.seq_query_ms * 1e6 / total_queries;
    auto rand_ns = r.rand_query_ms * 1e6 / total_queries;
    auto miss_ns = r.miss_query_ms * 1e6 / total_queries;

    std::printf("┌──────────────────────────────────────────────────────┐\n");
    std::printf("│ %-52s │\n", label);
    std::printf("├──────────────────────────┬───────────────────────────┤\n");
    std::printf("│ Dict entries             │ %25zu │\n", r.num_entries);
    std::printf("│ Query rounds             │ %25zu │\n", r.query_rounds);
    std::printf("│ Total queries / test     │ %25.0f │\n", total_queries);
    std::printf("├──────────────────────────┼───────────────────────────┤\n");
    std::printf("│ Build time               │ %22.2f ms │\n", r.build_ms);
    std::printf("├──────────────────────────┼───────────────────────────┤\n");
    std::printf("│ Seq query (all-hit)      │ %22.2f ms │\n", r.seq_query_ms);
    std::printf("│   per query              │ %22.1f ns │\n", seq_ns);
    std::printf("│   throughput             │ %19.2f Mq/s │\n", seq_qps / 1e6);
    std::printf("├──────────────────────────┼───────────────────────────┤\n");
    std::printf("│ Rand query (all-hit)     │ %22.2f ms │\n", r.rand_query_ms);
    std::printf("│   per query              │ %22.1f ns │\n", rand_ns);
    std::printf("│   throughput             │ %19.2f Mq/s │\n", rand_qps / 1e6);
    std::printf("├──────────────────────────┼───────────────────────────┤\n");
    std::printf("│ Miss query (all-miss)    │ %22.2f ms │\n", r.miss_query_ms);
    std::printf("│   per query              │ %22.1f ns │\n", miss_ns);
    std::printf("│   throughput             │ %19.2f Mq/s │\n", miss_qps / 1e6);
    std::printf("└──────────────────────────┴───────────────────────────┘\n\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// Dictionary loading — shared raw data
// ─────────────────────────────────────────────────────────────────────────────

struct RawEntry {
    std::string word;
    double freq;
    std::string tag;
};

static auto load_raw_entries(const std::string &dict_path) -> std::vector<RawEntry> {
    auto entries = std::vector<RawEntry>{};
    entries.reserve(350000);

    auto fd = ::open(dict_path.c_str(), O_RDONLY);
    if (fd == -1) {
        std::perror("open");
        return entries;
    }
    struct stat st{};
    ::fstat(fd, &st);
    auto file_size = static_cast<size_t>(st.st_size);
    const auto *data = static_cast<const char *>(::mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (data == MAP_FAILED) {
        ::close(fd);
        std::perror("mmap");
        return entries;
    }
    ::madvise(const_cast<void *>(static_cast<const void *>(data)), file_size, MADV_SEQUENTIAL);

    const auto *p = data;
    const auto *end = data + file_size;
    while (p < end) {
        const auto *line_end = static_cast<const char *>(std::memchr(p, '\n', end - p));
        if (!line_end) {
            line_end = end;
        }
        auto line = std::string_view(p, line_end - p);
        p = line_end + 1;
        if (line.empty()) {
            continue;
        }
        if (line.back() == '\r') {
            line.remove_suffix(1);
        }

        auto sp1 = line.find(' ');
        if (sp1 == std::string_view::npos) {
            continue;
        }
        auto sp2 = line.find(' ', sp1 + 1);
        if (sp2 == std::string_view::npos) {
            continue;
        }

        auto freq = 0.0;
        for (auto c : line.substr(sp1 + 1, sp2 - sp1 - 1)) {
            if (c >= '0' && c <= '9') {
                freq = freq * 10 + (c - '0');
            }
        }
        entries.push_back({
            std::string(line.substr(0, sp1)),
            freq,
            std::string(line.substr(sp2 + 1)),
        });
    }

    ::munmap(const_cast<void *>(static_cast<const void *>(data)), file_size);
    ::close(fd);
    return entries;
}

// ─────────────────────────────────────────────────────────────────────────────
// Benchmark: Old Trie (cppjieba::Trie)
// ─────────────────────────────────────────────────────────────────────────────

static auto bench_old_trie(const std::vector<RawEntry> &entries, size_t query_rounds) -> BenchResult {
    namespace old = cppjieba;

    auto dict_units = std::vector<old::DictUnit>(entries.size());
    auto freq_sum = 0.0;
    for (const auto &e : entries) {
        freq_sum += e.freq;
    }

    for (auto i = size_t{0}; i < entries.size(); ++i) {
        auto runes = old::RuneStrArray{};
        old::DecodeUTF8RunesInString(entries[i].word, runes);
        dict_units[i].word.clear();
        for (const auto &runeStr : runes) {
            dict_units[i].word.push_back(runeStr.rune);
        }
        dict_units[i].weight = entries[i].freq > 0 ? std::log(entries[i].freq / freq_sum) : -20.0;
        dict_units[i].tag = entries[i].tag;
    }

    auto keys = std::vector<old::Unicode>(dict_units.size());
    auto ptrs = std::vector<const old::DictUnit *>(dict_units.size());
    for (auto i = size_t{0}; i < dict_units.size(); ++i) {
        keys[i] = dict_units[i].word;
        ptrs[i] = &dict_units[i];
    }

    auto rune_arrays = std::vector<old::RuneStrArray>(entries.size());
    for (auto i = size_t{0}; i < entries.size(); ++i) {
        old::DecodeUTF8RunesInString(entries[i].word, rune_arrays[i]);
    }

    auto shuffled = std::vector<size_t>(entries.size());
    std::iota(shuffled.begin(), shuffled.end(), 0);
    auto rng = std::mt19937{42};
    std::ranges::shuffle(shuffled, rng);

    auto miss_rune_arrays = std::vector<old::RuneStrArray>(entries.size());
    for (auto i = size_t{0}; i < entries.size(); ++i) {
        miss_rune_arrays[i] = rune_arrays[i];
        if (!miss_rune_arrays[i].empty()) {
            miss_rune_arrays[i][0].rune = 0x10FFFF;
        }
    }

    // ── Build ────────────────────────────────────────────────────────────
    auto *trie = static_cast<old::Trie *>(nullptr);
    auto t0 = Clock::now();
    trie = new old::Trie(keys, ptrs);
    auto t1 = Clock::now();
    auto build_ms = Ms(t1 - t0).count();

    // ── Sequential query (all-hit) ───────────────────────────────────────
    auto t2 = Clock::now();
    for (auto round = size_t{0}; round < query_rounds; ++round) {
        for (auto i = size_t{0}; i < entries.size(); ++i) {
            auto &ra = rune_arrays[i];
            const auto *result = trie->Find(ra.begin(), ra.end());
            DoNotOptimize(result);
        }
    }
    auto t3 = Clock::now();
    auto seq_ms = Ms(t3 - t2).count();

    // ── Random query (all-hit) ───────────────────────────────────────────
    auto t4 = Clock::now();
    for (auto round = size_t{0}; round < query_rounds; ++round) {
        for (auto idx : shuffled) {
            auto &ra = rune_arrays[idx];
            const auto *result = trie->Find(ra.begin(), ra.end());
            DoNotOptimize(result);
        }
    }
    auto t5 = Clock::now();
    auto rand_ms = Ms(t5 - t4).count();

    // ── Miss query ───────────────────────────────────────────────────────
    auto t6 = Clock::now();
    for (auto round = size_t{0}; round < query_rounds; ++round) {
        for (auto i = size_t{0}; i < entries.size(); ++i) {
            auto &ra = miss_rune_arrays[i];
            const auto *result = trie->Find(ra.begin(), ra.end());
            DoNotOptimize(result);
        }
    }
    auto t7 = Clock::now();
    auto miss_ms = Ms(t7 - t6).count();

    delete trie;
    return {build_ms, seq_ms, rand_ms, miss_ms, entries.size(), query_rounds};
}

// ─────────────────────────────────────────────────────────────────────────────
// Benchmark: Neo Trie (using neo_cppjieba::Trie from neo/Trie.hpp)
// ─────────────────────────────────────────────────────────────────────────────
static auto bench_neo_trie(const std::vector<RawEntry> &entries, size_t query_rounds) -> BenchResult {
    namespace neo = neo_cppjieba;

    auto keys = std::vector<neo::Unicode>(entries.size());
    auto values = std::vector<neo::DictUnit>(entries.size());

    auto freq_sum = 0.0;
    for (const auto &e : entries) {
        freq_sum += e.freq;
    }

    for (auto i = size_t{0}; i < entries.size(); ++i) {
        keys[i] = neo::decode(entries[i].word);
        auto w = entries[i].freq > 0 ? static_cast<float>(std::log(entries[i].freq / freq_sum)) : -20.0f;
        values[i] = {w, neo::PosTag(entries[i].tag)};
    }

    auto shuffled = std::vector<size_t>(entries.size());
    std::iota(shuffled.begin(), shuffled.end(), 0);
    auto rng = std::mt19937{42};
    std::ranges::shuffle(shuffled, rng);

    auto miss_keys = std::vector<neo::Unicode>(entries.size());
    for (auto i = size_t{0}; i < entries.size(); ++i) {
        miss_keys[i] = keys[i];
        if (!miss_keys[i].empty()) {
            miss_keys[i][0] = 0x10FFFF;
        }
    }

    // ── Build ────────────────────────────────────────────────────────────
    auto trie = neo::Trie{};
    auto t0 = Clock::now();
    trie.build(keys, values);
    auto t1 = Clock::now();
    auto build_ms = Ms(t1 - t0).count();

    // ── Sequential query (all-hit) ───────────────────────────────────────
    auto t2 = Clock::now();
    for (auto round = size_t{0}; round < query_rounds; ++round) {
        for (auto i = size_t{0}; i < entries.size(); ++i) {
            auto result = trie.find(std::span<const neo::Rune>{keys[i]});
            DoNotOptimize(result);
        }
    }
    auto t3 = Clock::now();
    auto seq_ms = Ms(t3 - t2).count();

    // ── Random query (all-hit) ───────────────────────────────────────────
    auto t4 = Clock::now();
    for (auto round = size_t{0}; round < query_rounds; ++round) {
        for (auto idx : shuffled) {
            auto result = trie.find(std::span<const neo::Rune>{keys[idx]});
            DoNotOptimize(result);
        }
    }
    auto t5 = Clock::now();
    auto rand_ms = Ms(t5 - t4).count();

    // ── Miss query ───────────────────────────────────────────────────────
    auto t6 = Clock::now();
    for (auto round = size_t{0}; round < query_rounds; ++round) {
        for (auto i = size_t{0}; i < entries.size(); ++i) {
            auto result = trie.find(std::span<const neo::Rune>{miss_keys[i]});
            DoNotOptimize(result);
        }
    }
    auto t7 = Clock::now();
    auto miss_ms = Ms(t7 - t6).count();

    return {build_ms, seq_ms, rand_ms, miss_ms, entries.size(), query_rounds};
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

auto main(int argc, char *argv[]) -> int {
    auto dict_path = std::string{"dict/jieba.dict.utf8"};
    if (argc > 1) {
        dict_path = argv[1];
    }

    constexpr auto QUERY_ROUNDS = size_t{10};

    std::printf("Loading dictionary: %s\n", dict_path.c_str());
    auto entries = load_raw_entries(dict_path);
    std::printf("Loaded %zu entries.\n\n", entries.size());
    std::printf("Query rounds: %zu  (total queries per test = %zu)\n\n", QUERY_ROUNDS, entries.size() * QUERY_ROUNDS);

    std::printf("Benchmarking Old Trie (unordered_map per node) ...\n");
    auto old_result = bench_old_trie(entries, QUERY_ROUNDS);
    print_report("Old Trie (unordered_map, pointer-based)", old_result);

    std::printf("Benchmarking Neo Trie (flat contiguous layout) ...\n");
    auto neo_result = bench_neo_trie(entries, QUERY_ROUNDS);
    print_report("Neo Trie (flat vector, BFS layout, binary search)", neo_result);

    // ── Comparison ───────────────────────────────────────────────────────
    std::printf("══════════════════════════════════════════════════════\n");
    std::printf("  COMPARISON  (speedup = Old time / Neo time)\n");
    std::printf("══════════════════════════════════════════════════════\n");
    auto ratio = [](double old_ms, double neo_ms) -> std::pair<double, const char *> {
        auto r = old_ms / neo_ms;
        return std::pair{r, neo_ms < old_ms ? "(Neo faster)" : "(Old faster)"};
    };
    auto [br, bl] = ratio(old_result.build_ms, neo_result.build_ms);
    auto [sr, sl] = ratio(old_result.seq_query_ms, neo_result.seq_query_ms);
    auto [rr, rl] = ratio(old_result.rand_query_ms, neo_result.rand_query_ms);
    auto [mr, ml] = ratio(old_result.miss_query_ms, neo_result.miss_query_ms);
    std::printf("  Build time       : %5.2fx  %s\n", br, bl);
    std::printf("  Seq query        : %5.2fx  %s\n", sr, sl);
    std::printf("  Random query     : %5.2fx  %s\n", rr, rl);
    std::printf("  Miss query       : %5.2fx  %s\n", mr, ml);
    std::printf("══════════════════════════════════════════════════════\n");

    return 0;
}
