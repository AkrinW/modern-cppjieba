#include "gtest/gtest.h"
#include "neo/Config.hpp"
#include "neo/FileIO.hpp"
#include "neo/StringUtil.hpp"
#include "neo/Trie.hpp"
#include "neo/TrieStats.hpp"

#include "test_paths.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <vector>

using namespace neo_cppjieba;

TEST(TrieStatsTest, FindsWordsAcrossConfiguredFanoutThreshold) {
    const auto fanouts = std::array{TrieConfig::flat_threshold, TrieConfig::flat_threshold + 1};
    for (const auto fanout : fanouts) {
        auto keys = std::vector<Unicode>{};
        auto values = std::vector<DictUnit>{};
        keys.reserve(fanout);
        values.reserve(fanout);
        for (auto i = std::size_t{0}; i < fanout; ++i) {
            keys.push_back(Unicode{static_cast<Rune>(U'\u4E00' + i)});
            values.push_back({static_cast<float>(i + 1), PosTag{"n"}});
        }
        auto trie = Trie{};
        trie.build(keys, values);
        for (auto i = std::size_t{0}; i < keys.size(); ++i) {
            const auto found = trie.find(std::span<const Rune>{keys[i]});
            EXPECT_FLOAT_EQ(found.weight, values[i].weight);
            EXPECT_EQ(found.tag, values[i].tag);
        }
        EXPECT_FALSE(trie.find(U"\u9FFF").has_value());
    }
}

/// Helper: load jieba.dict.utf8 and build a neo Trie.
/// Format per line: "word freq tag"  (space-delimited, UTF-8).
namespace {
static auto build_jieba_trie() -> std::pair<Trie, size_t> {
    auto dict_path = std::string(DICT_DIR) + "/jieba.dict.utf8";
    auto file = read_file(dict_path);
    auto content = file.content();

    // Collect entries: word, freq, tag
    struct RawEntry {
        std::string_view word;
        double freq;
        std::string_view tag;
    };
    auto entries = std::vector<RawEntry>{};
    auto freq_sum = 0.0;

    auto lines = get_line_view(content);
    for (auto &&line : lines) {
        if (line.empty()) {
            continue;
        }
        auto fields = std::array<std::string_view, 3>{};
        auto fi = size_t{0};
        auto split = get_split_view(line, ' ');
        for (auto &&field : split) {
            if (fi < 3) {
                fields[fi] = field;
            }
            ++fi;
        }
        if (fi < 2) {
            continue;
        }

        auto freq = 0.0;
        auto &&[ptr, ec] = std::from_chars(fields[1].data(), fields[1].data() + fields[1].size(), freq);
        if (ec != std::errc{}) {
            continue;
        }

        freq_sum += freq;
        entries.push_back({fields[0], freq, fi >= 3 ? fields[2] : std::string_view{}});
    }

    auto keys = std::vector<Unicode>{};
    auto values = std::vector<DictUnit>{};
    keys.reserve(entries.size());
    values.reserve(entries.size());

    for (auto &e : entries) {
        keys.push_back(decode(e.word));
        auto w = e.freq > 0 ? static_cast<float>(std::log(e.freq / freq_sum)) : -20.0f;
        values.push_back({w, PosTag(e.tag)});
    }

    auto trie = Trie{};
    trie.build(keys, values);
    return std::pair{std::move(trie), keys.size()};
}
} // namespace
// ─── Basic TrieStats on a small hand-built trie ─────────────────────────────

TEST(TrieStatsTest, SmallTrie) {
    // Build a tiny trie with 3 keys: "ab", "ac", "abd"
    auto keys = std::vector<Unicode>{
        {U'a', U'b'},
        {U'a', U'c'},
        {U'a', U'b', U'd'},
    };
    auto values = std::vector<DictUnit>{
        {1.0f, PosTag("n")},
        {2.0f, PosTag("v")},
        {3.0f, PosTag("a")},
    };

    auto trie = Trie{};
    trie.build(keys, values);

    auto stats = trie.collect_stats();

    // Trie shape (lazy insert — no child nodes for terminal edges):
    //   root(0) --a--> (1) --b[val]--> (2) --d[val]--> [-1]
    //                       \--c[val]--> [-1]
    // Nodes: 3, Edges: 4, Lazy edges: 2, Leaves: 0, Values: 3
    EXPECT_EQ(stats.node_count, 3u);
    EXPECT_EQ(stats.edge_count, 4u);
    EXPECT_EQ(stats.value_count, 3u);
    EXPECT_EQ(stats.lazy_edge_count, 2u);
    EXPECT_EQ(stats.leaf_count, 0u);

    // Depths: root=0, (1)=1, (2)=2 → max=2
    EXPECT_EQ(stats.max_depth, 2u);

    // Max fanout: node(1) has 2 children → max_fanout = 2
    EXPECT_EQ(stats.max_fanout, 2u);

    // to_string should not be empty
    auto report = stats.to_string();
    EXPECT_FALSE(report.empty());
}

// ─── Empty Trie ──────────────────────────────────────────────────────────────

TEST(TrieStatsTest, EmptyTrie) {
    auto trie = Trie{};
    auto stats = trie.collect_stats();

    EXPECT_EQ(stats.node_count, 0u);
    EXPECT_EQ(stats.edge_count, 0u);
    EXPECT_EQ(stats.value_count, 0u);
    EXPECT_EQ(stats.leaf_count, 0u);
    EXPECT_EQ(stats.max_depth, 0u);
    EXPECT_EQ(stats.max_fanout, 0u);
    EXPECT_EQ(stats.total_estimated_bytes, 0u);
}

// ─── Single-key Trie ─────────────────────────────────────────────────────────

TEST(TrieStatsTest, SingleKey) {
    auto keys = std::vector<Unicode>{{U'x'}};
    auto values = std::vector<DictUnit>{{5.0f, PosTag("n")}};

    auto trie = Trie{};
    trie.build(keys, values);

    auto stats = trie.collect_stats();

    // root(0) --x[val]--> [-1]   nodes=1, edges=1, lazy=1, values=1, leaves=0
    EXPECT_EQ(stats.node_count, 1u);
    EXPECT_EQ(stats.edge_count, 1u);
    EXPECT_EQ(stats.value_count, 1u);
    EXPECT_EQ(stats.lazy_edge_count, 1u);
    EXPECT_EQ(stats.leaf_count, 0u);
    EXPECT_EQ(stats.max_depth, 0u);
    EXPECT_EQ(stats.max_fanout, 1u);
}

// ─── jieba.dict.utf8 full stats ──────────────────────────────────────────────

TEST(TrieStatsTest, JiebaDict) {
    auto &&[trie, dict_size] = build_jieba_trie();

    ASSERT_FALSE(trie.empty());
    ASSERT_GT(dict_size, 300000u); // jieba.dict has ~349k entries

    auto stats = trie.collect_stats();

    // Print the full report for visibility
    std::println("\n{}", stats.to_string());

    // ── Sanity checks on jieba.dict structure ────────────────────────────

    // Node count is smaller than before due to lazy insert (terminal edges
    // don't allocate child nodes). Still more nodes than keys.
    EXPECT_GT(stats.node_count, 100000u); // reduced from old 400k+ due to lazy insert

    // With lazy insert: edge_count = node_count - 1 + lazy_edge_count
    EXPECT_EQ(stats.edge_count, stats.node_count - 1 + stats.lazy_edge_count);

    // Lazy edge count should be significant (many terminal edges).
    EXPECT_GT(stats.lazy_edge_count, 100000u);

    // Value count should match the number of inserted dictionary entries.
    EXPECT_EQ(stats.value_count, dict_size);

    // With lazy insert, leaf_count is 0 (every materialized node has edges).
    EXPECT_EQ(stats.leaf_count, 0u);

    // Chinese words are typically 1-8 characters; max depth shouldn't be extreme.
    EXPECT_GE(stats.max_depth, 2u);
    EXPECT_LE(stats.max_depth, 50u); // generous upper bound

    // Root must have a large fanout (thousands of distinct first-characters in CJK).
    EXPECT_GT(stats.max_fanout, 500u);

    // Average fanout of non-leaf nodes: should be modest (most internal nodes have few children).
    EXPECT_GT(stats.avg_fanout, 1.0);
    EXPECT_LT(stats.avg_fanout, 20.0);

    // Memory: should be in the tens of MB range for ~350k entries.
    EXPECT_GT(stats.total_estimated_bytes, 1'000'000u);   // > 1 MB
    EXPECT_LT(stats.total_estimated_bytes, 500'000'000u); // < 500 MB

    // Depth histogram should have entries.
    EXPECT_FALSE(stats.depth_histogram.empty());
    // Root (depth 0) has exactly 1 node.
    EXPECT_EQ(stats.depth_histogram[0], 1u);

    // Fanout histogram should have entries.
    EXPECT_FALSE(stats.fanout_histogram.empty());

    // Average load factor should be between 0 and 1 (healthy hash maps).
    EXPECT_GT(stats.avg_load_factor, 0.0);
    EXPECT_LE(stats.avg_load_factor, 1.0);
}
