/// Benchmark: dictionary loading from text versus snapshots
///
/// Times each stage a service pays at startup, over several rounds:
///
///   1. Text DictTrie        — parse the main and user dictionaries and build the trie word by word
///   2. Snapshot decode      — check and decode snapshot bytes into the level-order dictionary
///   3. Snapshot DictTrie    — decode and build from memory, then from a file read into owned memory
///   4. HMModel              — the model every Jieba also loads
///   5. Jieba                — complete construction from text and from a snapshot file
///
/// With --cold every input file is evicted from the page cache before each round (Linux only), approximating a
/// service starting after its files left the cache. Timings are reported only after snapshot and text loading
/// cut the corpus identically.

#include "neo/Jieba.hpp"
#include "neo/options/UserWordWeightOption.hpp"
#include "neo/resource/DictSnapshot.hpp"
#include "neo/resource/DictTrie.hpp"
#include "neo/resource/HMModel.hpp"

#include "BenchmarkUtils.hpp"
#include "test_paths.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace neo_cppjieba;

namespace {

// Whether each round first evicts its input files from the page cache.
enum class PageCache { Warm, Cold };

/// Elapsed milliseconds of one case across its rounds.
struct Timing {
    double min_ms;
    double median_ms;
    double max_ms;
};

/// Command-line settings.
struct Settings {
    PageCache cache;
    int rounds;
};

// Drop the clean cached pages of a file so the next read comes from storage; only Linux allows this without root.
auto evict_from_page_cache(const std::filesystem::path &path) -> bool {
#ifdef __linux__
    const auto fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd == -1) {
        return false;
    }
    const auto advised = ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED) == 0;
    ::close(fd);
    return advised;
#else
    static_cast<void>(path);
    return false;
#endif
}

// Time one case; its result is destroyed after the clock stops, so teardown stays out of the timing.
template <typename Load>
auto time_case(const Settings &settings, std::span<const std::filesystem::path> inputs, Load load) -> Timing {
    auto samples = std::vector<double>{};
    samples.reserve(static_cast<std::size_t>(settings.rounds));
    for (auto round = 0; round < settings.rounds; ++round) {
        if (settings.cache == PageCache::Cold) {
            std::ranges::for_each(inputs, evict_from_page_cache);
        }
        const auto start = Clock::now();
        const auto result = load();
        DoNotOptimize(result);
        samples.push_back(Ms{Clock::now() - start}.count());
    }
    std::ranges::sort(samples);
    return {samples.front(), samples[samples.size() / 2], samples.back()};
}

auto print_timing(std::string_view name, const Timing &timing) -> void {
    std::printf("  %-30.*s %10.2f %10.2f %10.2f\n", static_cast<int>(name.size()), name.data(), timing.min_ms,
                timing.median_ms, timing.max_ms);
}

// Parse [--cold] [--rounds N]; returns false after printing usage for anything else.
auto parse_settings(std::span<char *const> args, Settings &settings) -> bool {
    for (auto i = std::size_t{1}; i < args.size(); ++i) {
        const auto arg = std::string_view{args[i]};
        if (arg == "--cold") {
            settings.cache = PageCache::Cold;
            continue;
        }
        if (arg == "--rounds" && i + 1 < args.size()) {
            const auto value = std::string_view{args[++i]};
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), settings.rounds);
            if (error == std::errc{} && end == value.data() + value.size() && settings.rounds > 0) {
                continue;
            }
        }
        std::fprintf(stderr, "usage: %s [--cold] [--rounds N]\n", args[0]);
        return false;
    }
    return true;
}

auto write_file(const std::filesystem::path &path, std::span<const std::byte> bytes) -> void {
    auto output = std::ofstream{path, std::ios::binary};
    output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// Snapshot and text loading must cut and tag the corpus identically before their timings mean anything.
auto same_cuts(const Jieba &snapshot, const Jieba &text, std::span<const std::string> lines) -> bool {
    return std::ranges::all_of(lines, [&](const std::string &line) {
        return snapshot.cut_strings<Tagging::Pos>(line, CutMode::MIX)
               == text.cut_strings<Tagging::Pos>(line, CutMode::MIX);
    });
}

auto megabytes(std::uintmax_t bytes) -> double {
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

} // namespace

auto main(int argc, char *argv[]) -> int {
    auto settings = Settings{PageCache::Warm, 9};
    if (!parse_settings(std::span{argv, static_cast<std::size_t>(argc)}, settings)) {
        return EXIT_FAILURE;
    }
    const auto dict_path = std::filesystem::path{DICT_DIR "/jieba.dict.utf8"};
    const auto user_path = std::filesystem::path{DICT_DIR "/user.dict.utf8"};
    const auto model_path = std::filesystem::path{DICT_DIR "/hmm_model.utf8"};
    const auto dict = DictSource::file(dict_path.string());
    const auto user_dicts = std::array{DictSource::file(user_path.string())};
    const auto model = DictSource::file(model_path.string());
    const auto opt = detail::UserWordWeightOption::WordWeightMedian;

    const auto encode_start = Clock::now();
    const auto bytes = Jieba::encode_dict_snapshot(dict, user_dicts);
    const auto encode_ms = Ms{Clock::now() - encode_start}.count();
    auto error = std::error_code{};
    const auto directory = std::filesystem::temp_directory_path() / "neo-load-benchmark";
    std::filesystem::create_directories(directory, error);
    const auto snapshot_path = directory / "jieba.snapshot";
    write_file(snapshot_path, bytes);
    const auto snapshot = SnapshotSource::file(snapshot_path.string());

    const auto snapshot_jieba = Jieba::from_snapshot(snapshot, model);
    const auto text_jieba = Jieba{dict, model, user_dicts};
    const auto lines = load_lines(TEST_DATA_DIR "/weicheng.utf8");
    if (!same_cuts(snapshot_jieba, text_jieba, lines)) {
        std::fprintf(stderr, "snapshot and text loading cut the corpus differently\n");
        std::filesystem::remove_all(directory, error);
        return EXIT_FAILURE;
    }

    const auto cold = settings.cache == PageCache::Cold;
    if (cold && !evict_from_page_cache(dict_path)) {
        std::printf("Page cache eviction is unavailable here; the cold run measures a warm cache.\n");
    }
    std::printf("Main dictionary %.2f MiB, user dictionary %ju B; snapshot %.2f MiB, encoded in %.2f ms\n",
                megabytes(std::filesystem::file_size(dict_path)), std::filesystem::file_size(user_path),
                megabytes(bytes.size()), encode_ms);
    std::printf("Page cache: %s, rounds: %d, corpus cuts match over %zu lines\n\n", cold ? "cold" : "warm",
                settings.rounds, lines.size());
    std::printf("  %-30s %10s %10s %10s\n", "case (ms)", "min", "median", "max");

    const auto text_inputs = std::array{dict_path, user_path};
    const auto snapshot_inputs = std::array{snapshot_path};
    const auto model_inputs = std::array{model_path};
    const auto text_jieba_inputs = std::array{dict_path, user_path, model_path};
    const auto snapshot_jieba_inputs = std::array{snapshot_path, model_path};
    const auto memory = SnapshotSource::memory(bytes, "jieba.snapshot");

    print_timing("text DictTrie",
                 time_case(settings, text_inputs, [&] { return detail::DictTrie{dict, user_dicts, opt}; }));
    print_timing("snapshot decode (memory)",
                 time_case(settings, {}, [&] { return detail::decode_dict_snapshot(bytes, "jieba.snapshot"); }));
    print_timing("snapshot DictTrie (memory)",
                 time_case(settings, {}, [&] { return detail::load_dict_snapshot(memory); }));
    print_timing("snapshot DictTrie (file)",
                 time_case(settings, snapshot_inputs, [&] { return detail::load_dict_snapshot(snapshot); }));
    print_timing("HMModel", time_case(settings, model_inputs, [&] { return detail::HMModel{model}; }));
    const auto text_total = time_case(settings, text_jieba_inputs, [&] { return Jieba{dict, model, user_dicts}; });
    const auto snapshot_total =
        time_case(settings, snapshot_jieba_inputs, [&] { return Jieba::from_snapshot(snapshot, model); });
    print_timing("Jieba from text", text_total);
    print_timing("Jieba from snapshot (file)", snapshot_total);
    std::printf("\nJieba construction speedup (median): %.2fx\n", text_total.median_ms / snapshot_total.median_ms);

    std::filesystem::remove_all(directory, error);
    return EXIT_SUCCESS;
}
