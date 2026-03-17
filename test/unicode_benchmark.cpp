/// Benchmark: Normal encode vs Fast encode (with source offsets)
///
/// Compares:
///   1. Decode overhead: decode() vs decode_with_source()
///   2. Full encode:     encode(span<Rune>) vs encode_range(0, n)
///   3. Sub-range encode: encode(subspan) vs encode_range(start, count)
///   4. Segmentation scenario: decode + cut into WordRanges + encode each word
///
/// Compile:
///   g++ -O2 -std=c++26 -I include -I deps/limonp/include test/unicode_benchmark.cpp -o build/unicode_benchmark
///
/// Run:
///   ./build/unicode_benchmark [path/to/jieba.dict.utf8]

#include "neo/FileIO.hpp"
#include "neo/StringUtil.hpp"
#include "neo/Unicode.hpp"

#include "BenchmarkUtils.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <span>
#include <string>
#include <vector>

using namespace neo_cppjieba;

namespace {

/// Build a large UTF-8 test string by concatenating all dictionary words.
auto build_test_string(const std::string &dict_path) -> std::string {
    auto mf = get_map_file(dict_path);
    auto content = mf.content();

    auto result = std::string{};
    result.reserve(content.size());

    auto lines = get_line_view(content);
    for (auto &&line : lines) {
        if (line.empty()) {
            continue;
        }
        // Each line: "word freq [tag]" — take only the word (first field)
        auto split = get_split_view(line, ' ');
        for (auto &&field : split) {
            result.append(field);
            break; // only first field
        }
    }
    return result;
}

struct BenchResult {
    double decode_normal_ms;
    double decode_sourced_ms;
    double encode_normal_ms;
    double encode_fast_ms;
    double subrange_normal_ms;
    double subrange_fast_ms;
};

auto run_benchmark(const std::string &text, int iterations) -> BenchResult {
    auto result = BenchResult{};

    // Warm up
    {
        auto u = decode(text);
        auto s = encode(std::span<const Rune>(u));
        DoNotOptimize(s);
        auto ws = decode_with_offset(text);
        auto view = as_view(text);
        const auto &offsets = ws.get_offsets();
        auto range = WordRange{0, static_cast<uint32_t>(ws.get_runes().size())};
        auto f = encode(view, offsets, range);
        DoNotOptimize(f);
    }

    // ─── 1. Decode benchmark ────────────────────────────────────────────
    {
        auto start = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            auto u = decode(text);
            DoNotOptimize(u);
        }
        result.decode_normal_ms = std::chrono::duration_cast<Ms>(Clock::now() - start).count() / iterations;
    }

    {
        auto start = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            auto ws = decode_with_offset(text);
            DoNotOptimize(ws);
        }
        result.decode_sourced_ms = std::chrono::duration_cast<Ms>(Clock::now() - start).count() / iterations;
    }

    // Pre-decode for encode benchmarks
    auto normal_decoded = decode(text);
    auto sourced_decoded = decode_with_offset(text);

    // ─── 2. Full encode benchmark ───────────────────────────────────────
    {
        auto start = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            auto s = encode(std::span<const Rune>(normal_decoded));
            DoNotOptimize(s);
        }
        result.encode_normal_ms = std::chrono::duration_cast<Ms>(Clock::now() - start).count() / iterations;
    }

    {
        auto start = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            auto view = as_view(text);
            const auto &offsets = sourced_decoded.get_offsets();
            auto range = WordRange{0, static_cast<uint32_t>(sourced_decoded.get_runes().size())};
            auto s = encode(view, offsets, range);
            DoNotOptimize(s);
        }
        result.encode_fast_ms = std::chrono::duration_cast<Ms>(Clock::now() - start).count() / iterations;
    }

    // ─── 3. Sub-range encode benchmark ──────────────────────────────────
    // Encode 100-rune chunks across the string
    auto chunk_size = size_t{100};
    auto num_chunks = normal_decoded.size() / chunk_size;
    if (num_chunks == 0) {
        num_chunks = 1;
    }

    {
        auto start = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            for (size_t c = 0; c < num_chunks; ++c) {
                auto offset = c * chunk_size;
                auto count = std::min(chunk_size, normal_decoded.size() - offset);
                auto s = encode(std::span<const Rune>(normal_decoded.data() + offset, count));
                DoNotOptimize(s);
            }
        }
        result.subrange_normal_ms = std::chrono::duration_cast<Ms>(Clock::now() - start).count() / iterations;
    }

    {
        auto start = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            for (size_t c = 0; c < num_chunks; ++c) {
                auto offset = c * chunk_size;
                auto count = std::min(chunk_size, sourced_decoded.runes.size() - offset);
                auto view = as_view(text);
                const auto &offsets = sourced_decoded.get_offsets();
                auto range = WordRange{static_cast<uint32_t>(offset), static_cast<uint32_t>(offset + count)};
                auto s = encode(view, offsets, range);
                DoNotOptimize(s);
            }
        }
        result.subrange_fast_ms = std::chrono::duration_cast<Ms>(Clock::now() - start).count() / iterations;
    }

    return result;
}

/// Simulate segmentation: generate realistic word ranges (avg ~2 runes per word)
/// that partition [0, total_runes) without gaps or overlaps.
auto simulate_segmentation(size_t total_runes, uint32_t seed = 42) -> std::vector<WordRange> {
    auto rng = std::mt19937{seed};
    // Word lengths: 1-4 runes, weighted towards 2 (typical Chinese segmentation)
    auto len_dist = std::discrete_distribution<uint32_t>{{15, 45, 30, 10}}; // P(1)=15%, P(2)=45%, P(3)=30%, P(4)=10%

    auto ranges = std::vector<WordRange>{};
    ranges.reserve(total_runes / 2);
    auto pos = uint32_t{0};
    auto limit = static_cast<uint32_t>(total_runes);
    while (pos < limit) {
        auto len = len_dist(rng) + 1; // 1..4
        if (pos + len > limit) {
            len = limit - pos;
        }
        ranges.push_back({pos, pos + len});
        pos += len;
    }
    return ranges;
}

struct SegBenchResult {
    size_t num_words;
    double normal_ms; // decode + encode each range
    double fast_ms;   // decode_with_source + encode_range each range
};

/// Benchmark: full decode→segment→encode pipeline
auto run_seg_benchmark(const std::string &text, const std::vector<WordRange> &ranges, int iterations)
    -> SegBenchResult {
    auto result = SegBenchResult{};
    result.num_words = ranges.size();

    // Warm up
    {
        auto runes = decode(text);
        auto words = std::vector<std::string>{};
        words.reserve(ranges.size());
        for (const auto &r : ranges) {
            words.push_back(encode(std::span<const Rune>(runes.data() + r.begin, r.size())));
        }
        DoNotOptimize(words);
    }

    // ─── Normal path: decode + encode(subspan) per word ─────────────────
    {
        auto start = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            auto runes = decode(text);
            auto words = std::vector<std::string>{};
            words.reserve(ranges.size());
            for (const auto &r : ranges) {
                words.push_back(encode(std::span<const Rune>(runes.data() + r.begin, r.size())));
            }
            DoNotOptimize(words);
        }
        result.normal_ms = std::chrono::duration_cast<Ms>(Clock::now() - start).count() / iterations;
    }

    // ─── Fast path: decode_with_source + encode_range per word ──────────
    {
        auto start = Clock::now();
        for (int i = 0; i < iterations; ++i) {
            auto ws = decode_with_offset(text);
            auto words = std::vector<std::string>{};
            words.reserve(ranges.size());
            auto view = as_view(text);
            const auto &offsets = ws.get_offsets();
            for (const auto &r : ranges) {
                words.push_back(encode(view, offsets, r));
            }
            DoNotOptimize(words);
        }
        result.fast_ms = std::chrono::duration_cast<Ms>(Clock::now() - start).count() / iterations;
    }

    return result;
}

} // namespace

auto main(int argc, char *argv[]) -> int {
    auto dict_path = std::string{argc > 1 ? argv[1] : "dict/jieba.dict.utf8"};

    std::printf("Building test string from: %s\n", dict_path.c_str());
    auto text = build_test_string(dict_path);
    std::printf("Test string: %zu bytes, ", text.size());
    auto rune_count = decode(text).size();
    std::printf("%zu runes\n\n", rune_count);

    constexpr auto ITERATIONS = 100;
    std::printf("Running %d iterations per benchmark...\n\n", ITERATIONS);

    auto r = run_benchmark(text, ITERATIONS);

    std::printf("═══════════════════════════════════════════════════════════════\n");
    std::printf("  Benchmark: Normal encode vs Fast encode (source offsets)\n");
    std::printf("═══════════════════════════════════════════════════════════════\n\n");

    std::printf("  ┌─────────────────────┬──────────────┬──────────────┬──────────┐\n");
    std::printf("  │ Operation           │ Normal (ms)  │ Fast (ms)    │ Speedup  │\n");
    std::printf("  ├─────────────────────┼──────────────┼──────────────┼──────────┤\n");
    std::printf("  │ Decode              │ %10.3f  │ %10.3f  │ %6.2fx  │\n", r.decode_normal_ms, r.decode_sourced_ms,
                r.decode_normal_ms / r.decode_sourced_ms);
    std::printf("  │ Full Encode         │ %10.3f  │ %10.3f  │ %6.2fx  │\n", r.encode_normal_ms, r.encode_fast_ms,
                r.encode_normal_ms / r.encode_fast_ms);
    std::printf("  │ Sub-range Encode    │ %10.3f  │ %10.3f  │ %6.2fx  │\n", r.subrange_normal_ms, r.subrange_fast_ms,
                r.subrange_normal_ms / r.subrange_fast_ms);
    std::printf("  └─────────────────────┴──────────────┴──────────────┴──────────┘\n\n");

    // Total roundtrip comparison
    auto total_normal = r.decode_normal_ms + r.encode_normal_ms;
    auto total_fast = r.decode_sourced_ms + r.encode_fast_ms;
    std::printf("  Total roundtrip (decode + full encode):\n");
    std::printf("    Normal: %.3f ms\n", total_normal);
    std::printf("    Fast:   %.3f ms\n", total_fast);
    std::printf("    Speedup: %.2fx\n\n", total_normal / total_fast);

    // ─── 4. Segmentation scenario ───────────────────────────────────────
    std::printf("═══════════════════════════════════════════════════════════════\n");
    std::printf("  Benchmark: Segmentation scenario (decode → cut → encode)\n");
    std::printf("═══════════════════════════════════════════════════════════════\n\n");

    auto ranges = simulate_segmentation(rune_count);
    std::printf("  Simulated segmentation: %zu words from %zu runes (avg %.1f runes/word)\n\n", ranges.size(),
                rune_count, static_cast<double>(rune_count) / ranges.size());

    auto sr = run_seg_benchmark(text, ranges, ITERATIONS);

    std::printf("  ┌──────────────────────────────┬──────────────┬──────────────┬──────────┐\n");
    std::printf("  │ Pipeline                     │     Time(ms) │     Time(ms) │ Speedup  │\n");
    std::printf("  │ (decode + encode all words)   │   Normal     │   Fast       │          │\n");
    std::printf("  ├──────────────────────────────┼──────────────┼──────────────┼──────────┤\n");
    std::printf("  │ %6zu words                  │ %10.3f  │ %10.3f  │ %6.2fx  │\n", sr.num_words, sr.normal_ms,
                sr.fast_ms, sr.normal_ms / sr.fast_ms);
    std::printf("  └──────────────────────────────┴──────────────┴──────────────┴──────────┘\n\n");

    std::printf("  Per-word encode cost:\n");
    std::printf("    Normal: %.3f µs/word\n", sr.normal_ms * 1000.0 / sr.num_words);
    std::printf("    Fast:   %.3f µs/word\n", sr.fast_ms * 1000.0 / sr.num_words);
    std::printf("    Speedup: %.2fx\n\n", sr.normal_ms / sr.fast_ms);

    return 0;
}
