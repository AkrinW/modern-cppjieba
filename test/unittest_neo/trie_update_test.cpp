#include "gtest/gtest.h"
#include "neo/Config.hpp"
#include "neo/resource/DictUnit.hpp"
#include "neo/resource/Trie.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

using namespace neo_cppjieba;
using namespace neo_cppjieba::detail;

namespace {

// Build a trie from parallel arrays so each test states its initial dictionary inline.
auto build_trie(std::span<const Unicode> keys, std::span<const DictUnit> values) -> Trie {
    auto trie = Trie{};
    trie.build(keys, values);
    return trie;
}

// Collect real dictionary match ends from the first rune.
auto match_ends(const Trie &trie, std::span<const Rune> runes) -> std::vector<RuneIndex> {
    auto ends = std::vector<RuneIndex>{};
    trie.for_each_match_from(runes, 0, [&](RuneIndex end, const DictUnit &) { ends.push_back(end); });
    return ends;
}

} // namespace

TEST(TrieUpdateTest, InsertAddsWordAlongNewPath) {
    const auto keys = std::array{Unicode{U'中', U'国'}};
    const auto values = std::array{DictUnit{.weight = -1.0f}};
    auto trie = build_trie(keys, values);
    const auto word = Unicode{U'中', U'华', U'人', U'民'};

    trie.insert(word, DictUnit{.weight = -2.0f});

    EXPECT_FLOAT_EQ(trie.find(word).weight, -2.0f);
    EXPECT_FLOAT_EQ(trie.find(keys[0]).weight, -1.0f);
    EXPECT_FALSE(trie.find(Unicode{U'中', U'华'}).has_value());
    const auto dag = trie.find_dag(word);
    const auto edges = dag.get_edges(0);
    ASSERT_EQ(edges.size(), 2u);
    EXPECT_EQ(edges.back().next_pos, RuneIndex{4});
    EXPECT_FLOAT_EQ(edges.back().weight, -2.0f);
}

TEST(TrieUpdateTest, InsertOnExistingPrefixAddsMatchWithoutNewNodes) {
    const auto keys = std::array{Unicode{U'中', U'华', U'人', U'民'}};
    const auto values = std::array{DictUnit{.weight = -1.0f}};
    auto trie = build_trie(keys, values);
    const auto nodes = trie.node_count();

    trie.insert(Unicode{U'中', U'华'}, DictUnit{.weight = -2.0f});

    EXPECT_EQ(trie.node_count(), nodes);
    EXPECT_EQ(match_ends(trie, keys[0]), (std::vector<RuneIndex>{2, 4}));
}

TEST(TrieUpdateTest, InsertExtendsWordThatEndedAtTerminalEdge) {
    const auto keys = std::array{Unicode{U'中'}};
    const auto values = std::array{DictUnit{.weight = -1.0f}};
    auto trie = build_trie(keys, values);
    const auto word = Unicode{U'中', U'国'};

    trie.insert(word, DictUnit{.weight = -2.0f});

    EXPECT_EQ(match_ends(trie, word), (std::vector<RuneIndex>{1, 2}));
}

TEST(TrieUpdateTest, InsertIntoFullCompactChildSetKeepsExistingWords) {
    auto keys = std::vector<Unicode>{};
    auto values = std::vector<DictUnit>{};
    for (auto child = Rune{0x10000}; child < 0x10004; ++child) {
        keys.push_back(Unicode{U'中', child});
        values.push_back(DictUnit{.weight = -static_cast<float>(keys.size())});
    }
    keys.push_back(Unicode{U'中', Rune{0x10000}, U'词'});
    values.push_back(DictUnit{.weight = -9.0f});
    auto trie = build_trie(keys, values);
    const auto word = Unicode{U'中', Rune{0x10004}};

    trie.insert(word, DictUnit{.weight = -7.0f});

    for (std::size_t i = 0; i < keys.size(); ++i) {
        EXPECT_FLOAT_EQ(trie.find(keys[i]).weight, values[i].weight);
    }
    EXPECT_FLOAT_EQ(trie.find(word).weight, -7.0f);
    const auto stats = trie.collect_stats();
    EXPECT_EQ(stats.edge_count, 7u);
    EXPECT_EQ(stats.value_count, 6u);
    EXPECT_EQ(stats.direct_root_edge_count, 1u);
    EXPECT_EQ(stats.compact_edge_count, 1u);
    EXPECT_EQ(stats.hashed_edge_count, 5u);
}

TEST(TrieUpdateTest, InsertAddsRootsOutsideBuiltRootTable) {
    const auto keys = std::array{Unicode{U'一'}};
    const auto values = std::array{DictUnit{.weight = -1.0f}};
    auto trie = build_trie(keys, values);
    const auto bmp = Unicode{U'龥'};
    const auto supplementary = Unicode{U'𠮷', U'甲'};

    trie.insert(bmp, DictUnit{.weight = -2.0f});
    trie.insert(supplementary, DictUnit{.weight = -3.0f});

    EXPECT_FLOAT_EQ(trie.find(keys[0]).weight, -1.0f);
    EXPECT_FLOAT_EQ(trie.find(bmp).weight, -2.0f);
    EXPECT_EQ(match_ends(trie, supplementary), (std::vector<RuneIndex>{2}));
}

TEST(TrieUpdateTest, InsertReplacesExistingValue) {
    const auto keys = std::array{Unicode{U'中', U'国'}};
    const auto values = std::array{DictUnit{.weight = -1.0f, .tag = PosTag{"n"}}};
    auto trie = build_trie(keys, values);

    trie.insert(keys[0], DictUnit{.weight = -3.0f, .tag = PosTag{"ns"}});

    const auto word = trie.find(keys[0]);
    EXPECT_FLOAT_EQ(word.weight, -3.0f);
    EXPECT_EQ(word.tag, "ns");
}

TEST(TrieUpdateTest, EraseRemovesOnlyTheGivenWord) {
    const auto keys = std::array{Unicode{U'中', U'华'}, Unicode{U'中', U'华', U'人', U'民'}};
    const auto values = std::array{DictUnit{.weight = -1.0f}, DictUnit{.weight = -2.0f}};
    auto trie = build_trie(keys, values);

    EXPECT_TRUE(trie.erase(keys[0]));

    EXPECT_FALSE(trie.find(keys[0]).has_value());
    EXPECT_FLOAT_EQ(trie.find(keys[1]).weight, -2.0f);
    EXPECT_EQ(match_ends(trie, keys[1]), (std::vector<RuneIndex>{4}));
}

TEST(TrieUpdateTest, EraseReportsWordsWithoutValues) {
    const auto keys = std::array{Unicode{U'中', U'华', U'人', U'民'}};
    const auto values = std::array{DictUnit{.weight = -1.0f}};
    auto trie = build_trie(keys, values);

    EXPECT_FALSE(trie.erase(Unicode{U'中', U'华'}));
    EXPECT_FALSE(trie.erase(Unicode{U'美', U'国'}));
    EXPECT_TRUE(trie.erase(keys[0]));
    EXPECT_FALSE(trie.erase(keys[0]));
}

TEST(TrieUpdateTest, InsertAfterEraseRestoresMatch) {
    const auto keys = std::array{Unicode{U'中', U'国'}};
    const auto values = std::array{DictUnit{.weight = -1.0f}};
    auto trie = build_trie(keys, values);
    ASSERT_TRUE(trie.erase(keys[0]));

    trie.insert(keys[0], DictUnit{.weight = -4.0f});

    EXPECT_FLOAT_EQ(trie.find(keys[0]).weight, -4.0f);
    EXPECT_EQ(match_ends(trie, keys[0]), (std::vector<RuneIndex>{2}));
}

TEST(TrieUpdateTest, CloneEditsIndependently) {
    const auto keys = std::array{Unicode{U'中', U'国'}};
    const auto values = std::array{DictUnit{.weight = -1.0f}};
    auto original = build_trie(keys, values);
    auto copy = original.clone();
    const auto word = Unicode{U'中', U'华'};

    copy.insert(word, DictUnit{.weight = -2.0f});
    ASSERT_TRUE(original.erase(keys[0]));

    EXPECT_FALSE(original.find(word).has_value());
    EXPECT_FLOAT_EQ(copy.find(word).weight, -2.0f);
    EXPECT_FLOAT_EQ(copy.find(keys[0]).weight, -1.0f);
}

TEST(TrieUpdateTest, CompactPreservesWordsAndRestoresCompactStorage) {
    auto keys = std::vector<Unicode>{};
    auto values = std::vector<DictUnit>{};
    for (auto child = Rune{0x10000}; child < 0x10004; ++child) {
        keys.push_back(Unicode{U'中', child});
        values.push_back(DictUnit{.weight = -static_cast<float>(keys.size())});
    }
    auto trie = build_trie(keys, values);
    const auto added = Unicode{U'中', Rune{0x10004}};
    trie.insert(added, DictUnit{.weight = -7.0f});
    ASSERT_TRUE(trie.erase(keys[0]));
    ASSERT_EQ(trie.collect_stats().compact_edge_count, 0u);

    trie.compact();

    EXPECT_FALSE(trie.find(keys[0]).has_value());
    for (std::size_t i = 1; i < keys.size(); ++i) {
        EXPECT_FLOAT_EQ(trie.find(keys[i]).weight, values[i].weight);
    }
    EXPECT_FLOAT_EQ(trie.find(added).weight, -7.0f);
    const auto stats = trie.collect_stats();
    EXPECT_EQ(stats.value_count, 4u);
    EXPECT_EQ(stats.compact_edge_count, 4u);
    EXPECT_EQ(stats.hashed_edge_count, 0u);
}

TEST(TrieUpdateTest, CompactDropsNodesLeftByErase) {
    const auto keys = std::array{Unicode{U'中', U'国'}, Unicode{U'中', U'华', U'人', U'民'}};
    const auto values = std::array{DictUnit{.weight = -1.0f}, DictUnit{.weight = -2.0f}};
    auto trie = build_trie(keys, values);
    const auto reference = build_trie(std::span{keys}.first(1), std::span{values}.first(1));
    ASSERT_TRUE(trie.erase(keys[1]));

    trie.compact();

    EXPECT_EQ(trie.node_count(), reference.node_count());
    EXPECT_FLOAT_EQ(trie.find(keys[0]).weight, -1.0f);
    EXPECT_FALSE(trie.find(keys[1]).has_value());
}

TEST(TrieUpdateTest, CompactAfterErasingEveryWordAcceptsNewWords) {
    const auto keys = std::array{Unicode{U'中', U'国'}};
    const auto values = std::array{DictUnit{.weight = -1.0f}};
    auto trie = build_trie(keys, values);
    ASSERT_TRUE(trie.erase(keys[0]));
    trie.compact();
    ASSERT_TRUE(trie.empty());
    const auto word = Unicode{U'美', U'国', U'人'};

    ASSERT_TRUE(trie.has_capacity_for(word));
    trie.insert(word, DictUnit{.weight = -3.0f});

    EXPECT_FALSE(trie.find(keys[0]).has_value());
    EXPECT_EQ(match_ends(trie, word), (std::vector<RuneIndex>{3}));
}
