/// Benchmark: Old DictTrie vs Neo DictTrie — query DictUnit performance
///
/// Builds both DictTrie objects from jieba.dict.utf8, pre-decodes all query
/// keys to Unicode *before* timing, then compares pure lookup throughput:
///
///   1. Sequential all-hit   — query every dict word in order
///   2. Random all-hit       — query every dict word in shuffled order
///   3. All-miss             — query words guaranteed not in the dict
///   4. Short miss           — single-rune queries that don't exist
///
/// The Unicode transcoding cost is explicitly excluded from all measurements.

// ── Old DictTrie (cppjieba namespace) ──
#include "cppjieba/DictTrie.hpp"

// ── Neo DictTrie (neo_cppjieba namespace) ──
#include "neo/detail/DictTrie.hpp"

#include "BenchmarkUtils.hpp"
#include "test_paths.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <sys/mman.h>
#include <sys/stat.h>

namespace {

struct BenchResult {
    double seq_query_ms;
    double rand_query_ms;
    double miss_query_ms;
    double short_miss_query_ms;
    size_t num_entries;
    size_t query_rounds;
};

static auto print_report(const char *label, const BenchResult &r) -> void {
    auto total = static_cast<double>(r.num_entries * r.query_rounds);
    auto fmt = [&](const char *name, double ms) {
        auto ns = ms * 1e6 / total;
        auto qps = total / (ms / 1000.0);
        std::printf("│ %-26s│ %18.2f ms │\n", name, ms);
        std::printf("│   per query               │ %18.1f ns │\n", ns);
        std::printf("│   throughput              │ %15.2f Mq/s │\n", qps / 1e6);
    };

    std::printf("┌────────────────────────────────────────────────────┐\n");
    std::printf("│ %-50s │\n", label);
    std::printf("├───────────────────────────┬────────────────────────┤\n");
    std::printf("│ Dict entries              │ %22zu │\n", r.num_entries);
    std::printf("│ Query rounds              │ %22zu │\n", r.query_rounds);
    std::printf("│ Total queries / test      │ %22.0f │\n", total);
    std::printf("├───────────────────────────┼────────────────────────┤\n");
    fmt("Seq query (all-hit)       ", r.seq_query_ms);
    std::printf("├───────────────────────────┼────────────────────────┤\n");
    fmt("Rand query (all-hit)      ", r.rand_query_ms);
    std::printf("├───────────────────────────┼────────────────────────┤\n");
    fmt("Miss query (mutated)      ", r.miss_query_ms);
    std::printf("├───────────────────────────┼────────────────────────┤\n");
    fmt("Short miss (1-rune)       ", r.short_miss_query_ms);
    std::printf("└───────────────────────────┴────────────────────────┘\n\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// Load raw UTF-8 words from dict file (word only, for query key generation)
// ─────────────────────────────────────────────────────────────────────────────
static auto load_words(const std::string &dict_path) -> std::vector<std::string> {
    auto words = std::vector<std::string>{};
    words.reserve(350000);

    auto fd = ::open(dict_path.c_str(), O_RDONLY);
    if (fd == -1) {
        std::perror("open");
        return words;
    }
    struct stat st{};
    ::fstat(fd, &st);
    auto file_size = static_cast<size_t>(st.st_size);
    const auto *data = static_cast<const char *>(::mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (data == MAP_FAILED) {
        ::close(fd);
        std::perror("mmap");
        return words;
    }

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

        auto sp = line.find(' ');
        if (sp == std::string_view::npos) {
            continue;
        }
        words.emplace_back(line.substr(0, sp));
    }

    ::munmap(const_cast<void *>(static_cast<const void *>(data)), file_size);
    ::close(fd);
    return words;
}

// ─────────────────────────────────────────────────────────────────────────────
// Benchmark: Old DictTrie (cppjieba::DictTrie)
// ─────────────────────────────────────────────────────────────────────────────
static auto bench_old(const std::string &dict_path, const std::vector<std::string> &words, size_t rounds)
    -> BenchResult {
    namespace old = cppjieba;

    // Build DictTrie (not timed — we only care about query performance)
    old::DictTrie dict_trie(dict_path);

    // ── Pre-decode all query keys (excluded from timing) ─────────────────
    auto n = words.size();

    auto rune_arrays = std::vector<old::RuneStrArray>(n);
    for (auto i = size_t{0}; i < n; ++i) {
        old::DecodeUTF8RunesInString(words[i], rune_arrays[i]);
    }

    // Shuffled indices
    auto shuffled = std::vector<size_t>(n);
    std::iota(shuffled.begin(), shuffled.end(), 0);
    auto rng = std::mt19937{42};
    std::ranges::shuffle(shuffled, rng);

    // Miss keys: replace the first rune with a code point that won't appear in any dict word
    auto miss_arrays = std::vector<old::RuneStrArray>(n);
    for (auto i = size_t{0}; i < n; ++i) {
        miss_arrays[i] = rune_arrays[i];
        if (!miss_arrays[i].empty()) {
            miss_arrays[i][0].rune = 0x10FFFF;
        }
    }

    // Short miss keys: single rune that is unlikely to be in the dict
    constexpr auto kMissRunes = std::array<old::Rune, 4>{0x10FFFE, 0x10FFFD, 0x10FFFC, 0x10FFFB};
    auto short_miss_arrays = std::vector<old::RuneStrArray>(n);
    for (auto i = size_t{0}; i < n; ++i) {
        old::RuneStr rs;
        rs.rune = kMissRunes[i % 4];
        rs.offset = 0;
        rs.len = 1;
        rs.unicode_offset = 0;
        rs.unicode_length = 1;
        short_miss_arrays[i].push_back(rs);
    }

    // ── Sequential query (all-hit) ───────────────────────────────────────
    auto t0 = Clock::now();
    for (auto round = size_t{0}; round < rounds; ++round) {
        for (auto i = size_t{0}; i < n; ++i) {
            auto &ra = rune_arrays[i];
            const auto *result = dict_trie.Find(ra.begin(), ra.end());
            DoNotOptimize(result);
        }
    }
    auto t1 = Clock::now();

    // ── Random query (all-hit) ───────────────────────────────────────────
    auto t2 = Clock::now();
    for (auto round = size_t{0}; round < rounds; ++round) {
        for (auto idx : shuffled) {
            auto &ra = rune_arrays[idx];
            const auto *result = dict_trie.Find(ra.begin(), ra.end());
            DoNotOptimize(result);
        }
    }
    auto t3 = Clock::now();

    // ── Miss query (mutated first rune) ──────────────────────────────────
    auto t4 = Clock::now();
    for (auto round = size_t{0}; round < rounds; ++round) {
        for (auto i = size_t{0}; i < n; ++i) {
            auto &ra = miss_arrays[i];
            const auto *result = dict_trie.Find(ra.begin(), ra.end());
            DoNotOptimize(result);
        }
    }
    auto t5 = Clock::now();

    // ── Short miss query (single rune) ───────────────────────────────────
    auto t6 = Clock::now();
    for (auto round = size_t{0}; round < rounds; ++round) {
        for (auto i = size_t{0}; i < n; ++i) {
            auto &ra = short_miss_arrays[i];
            const auto *result = dict_trie.Find(ra.begin(), ra.end());
            DoNotOptimize(result);
        }
    }
    auto t7 = Clock::now();

    return {
        Ms(t1 - t0).count(), Ms(t3 - t2).count(), Ms(t5 - t4).count(), Ms(t7 - t6).count(), n, rounds,
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Benchmark: Neo DictTrie (neo_cppjieba::DictTrie)
// ─────────────────────────────────────────────────────────────────────────────
static auto bench_neo(const std::string &dict_path, const std::vector<std::string> &words, size_t rounds)
    -> BenchResult {
    namespace neo = neo_cppjieba;

    // Build DictTrie (not timed)
    auto dict_trie = neo::DictTrie{dict_path};

    // ── Pre-decode all query keys (excluded from timing) ─────────────────
    auto n = words.size();

    auto keys = std::vector<neo::Unicode>(n);
    for (auto i = size_t{0}; i < n; ++i) {
        keys[i] = neo::decode(words[i]);
    }

    // Shuffled indices
    auto shuffled = std::vector<size_t>(n);
    std::iota(shuffled.begin(), shuffled.end(), 0);
    auto rng = std::mt19937{42};
    std::ranges::shuffle(shuffled, rng);

    // Miss keys: replace the first rune
    auto miss_keys = std::vector<neo::Unicode>(n);
    for (auto i = size_t{0}; i < n; ++i) {
        miss_keys[i] = keys[i];
        if (!miss_keys[i].empty()) {
            miss_keys[i][0] = 0x10FFFF;
        }
    }

    // Short miss keys: single rune
    constexpr auto kMissRunes = std::array<neo::Rune, 4>{0x10FFFE, 0x10FFFD, 0x10FFFC, 0x10FFFB};
    auto short_miss_keys = std::vector<neo::Unicode>(n);
    for (auto i = size_t{0}; i < n; ++i) {
        short_miss_keys[i] = {kMissRunes[i % 4]};
    }

    // ── Sequential query (all-hit) ───────────────────────────────────────
    auto t0 = Clock::now();
    for (auto round = size_t{0}; round < rounds; ++round) {
        for (auto i = size_t{0}; i < n; ++i) {
            auto result = dict_trie.find(std::span<const neo::Rune>{keys[i]});
            DoNotOptimize(result);
        }
    }
    auto t1 = Clock::now();

    // ── Random query (all-hit) ───────────────────────────────────────────
    auto t2 = Clock::now();
    for (auto round = size_t{0}; round < rounds; ++round) {
        for (auto idx : shuffled) {
            auto result = dict_trie.find(std::span<const neo::Rune>{keys[idx]});
            DoNotOptimize(result);
        }
    }
    auto t3 = Clock::now();

    // ── Miss query (mutated first rune) ──────────────────────────────────
    auto t4 = Clock::now();
    for (auto round = size_t{0}; round < rounds; ++round) {
        for (auto i = size_t{0}; i < n; ++i) {
            auto result = dict_trie.find(std::span<const neo::Rune>{miss_keys[i]});
            DoNotOptimize(result);
        }
    }
    auto t5 = Clock::now();

    // ── Short miss query (single rune) ───────────────────────────────────
    auto t6 = Clock::now();
    for (auto round = size_t{0}; round < rounds; ++round) {
        for (auto i = size_t{0}; i < n; ++i) {
            auto result = dict_trie.find(std::span<const neo::Rune>{short_miss_keys[i]});
            DoNotOptimize(result);
        }
    }
    auto t7 = Clock::now();

    return {
        Ms(t1 - t0).count(), Ms(t3 - t2).count(), Ms(t5 - t4).count(), Ms(t7 - t6).count(), n, rounds,
    };
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────
auto main(int argc, char *argv[]) -> int {
    auto dict_path = std::string(DICT_DIR) + "/jieba.dict.utf8";
    if (argc > 1) {
        dict_path = argv[1];
    }

    constexpr auto QUERY_ROUNDS = size_t{10};

    std::printf("Loading dictionary: %s\n", dict_path.c_str());
    auto words = load_words(dict_path);
    std::printf("Loaded %zu words for querying.\n", words.size());
    std::printf("Query rounds: %zu  (total queries per test = %zu)\n\n", QUERY_ROUNDS, words.size() * QUERY_ROUNDS);

    // ── Old DictTrie ─────────────────────────────────────────────────────
    std::printf("Benchmarking Old DictTrie ...\n");
    auto old_result = bench_old(dict_path, words, QUERY_ROUNDS);
    print_report("Old DictTrie (unordered_map per TrieNode, pointer)", old_result);

    // ── Neo DictTrie ─────────────────────────────────────────────────────
    std::printf("Benchmarking Neo DictTrie ...\n");
    auto neo_result = bench_neo(dict_path, words, QUERY_ROUNDS);
    print_report("Neo DictTrie (flat vector, BFS layout, inline value)", neo_result);

    // ── Comparison ───────────────────────────────────────────────────────
    auto cmp = [](const char *label, double old_ms, double neo_ms) {
        auto r = old_ms / neo_ms;
        const auto *tag = neo_ms < old_ms ? "(Neo faster)" : "(Old faster)";
        std::printf("  %-24s: %5.2fx  %s\n", label, r, tag);
    };

    std::printf("══════════════════════════════════════════════════════\n");
    std::printf("  COMPARISON  (speedup = Old time / Neo time)\n");
    std::printf("══════════════════════════════════════════════════════\n");
    cmp("Seq query (all-hit)", old_result.seq_query_ms, neo_result.seq_query_ms);
    cmp("Rand query (all-hit)", old_result.rand_query_ms, neo_result.rand_query_ms);
    cmp("Miss query (mutated)", old_result.miss_query_ms, neo_result.miss_query_ms);
    cmp("Short miss (1-rune)", old_result.short_miss_query_ms, neo_result.short_miss_query_ms);
    std::printf("══════════════════════════════════════════════════════\n");

    return 0;
}
