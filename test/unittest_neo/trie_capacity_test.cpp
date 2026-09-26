#include "gtest/gtest.h"
#include "neo/Config.hpp"
#include "neo/UnicodeTypes.hpp"
#include "neo/detail/Trie.hpp"
#include "neo/third_party/gtl.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <vector>

using namespace neo_cppjieba;

namespace {

using TransitionKey = detail::TrieTransitionKey<TrieNodeId>;
using TransitionTable = gtl::flat_hash_map<TransitionKey, int, detail::TrieTransitionHash>;

} // namespace

TEST(TrieCapacityTest, EveryParentIdBitKeepsAnIndependentTransition) {
    auto transitions = TransitionTable{};
    constexpr auto bits = std::numeric_limits<TrieNodeId>::digits;
    transitions.reserve(bits + 1);
    transitions.emplace(TransitionKey{0, U'𠮷'}, -1);
    for (int bit = 0; bit < bits; ++bit) {
        const auto parent = static_cast<TrieNodeId>(TrieNodeId{1} << bit);
        transitions.emplace(TransitionKey{parent, U'𠮷'}, bit);
    }

    ASSERT_EQ(transitions.size(), static_cast<std::size_t>(bits + 1));
    EXPECT_EQ(transitions.at(TransitionKey{0, U'𠮷'}), -1);
    for (int bit = 0; bit < bits; ++bit) {
        const auto parent = static_cast<TrieNodeId>(TrieNodeId{1} << bit);
        EXPECT_EQ(transitions.at(TransitionKey{parent, U'𠮷'}), bit);
    }
}

TEST(TrieCapacityTest, SupplementaryRuneBitsKeepTransitionsDistinct) {
    auto transitions = TransitionTable{};
    const auto bmp = TransitionKey{1, Rune{0x1234}};
    const auto supplementary = TransitionKey{1, Rune{0x11234}};
    transitions.emplace(bmp, 1);
    transitions.emplace(supplementary, 2);

    ASSERT_EQ(transitions.size(), 2u);
    EXPECT_EQ(transitions.at(bmp), 1);
    EXPECT_EQ(transitions.at(supplementary), 2);
}

TEST(TrieCapacityTest, LastUsableEightBitNodeIdRemainsSearchable) {
    auto keys = std::vector<Unicode>{};
    auto values = std::vector<DictUnit>{};
    keys.reserve(254);
    values.reserve(254);
    for (std::size_t i = 1; i <= 254; ++i) {
        keys.push_back(Unicode{static_cast<Rune>(0x100 + i), U'𠮷'});
        values.push_back(DictUnit{.weight = -static_cast<float>(i)});
    }

    auto trie = Trie{};
    trie.build(keys, values);
    ASSERT_EQ(trie.node_count(), 255u);
    for (std::size_t i = 0; i < keys.size(); ++i) {
        EXPECT_EQ(trie.find(keys[i]).weight, values[i].weight);
    }
    EXPECT_FALSE(trie.find(Unicode{Rune{0x100}, U'𠮷'}).has_value());
    EXPECT_FALSE(trie.find(Unicode{keys.back().front(), U'😀'}).has_value());

    const auto stats = trie.collect_stats();
    EXPECT_EQ(stats.node_count, 255u);
    EXPECT_EQ(stats.direct_root_edge_count, 254u);
    EXPECT_EQ(stats.hashed_edge_count + stats.compact_edge_count, 254u);
    EXPECT_EQ(stats.value_count, 254u);
    EXPECT_EQ(stats.max_depth, 1u);
}

TEST(TrieCapacityTest, DirectAndHashedRootsPreserveDagMatches) {
    const auto keys = std::array{Unicode{U'甲', U'𠮷'}, Unicode{U'𠮷', U'甲'}};
    const auto values = std::array{DictUnit{.weight = -1.0f}, DictUnit{.weight = -2.0f}};
    auto trie = Trie{};
    trie.build(keys, values);

    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto dag = trie.find_dag(keys[i]);
        const auto edges = dag.get_edges(0);
        ASSERT_EQ(edges.size(), 2u);
        EXPECT_EQ(edges.back().next_pos, RuneIndex{2});
        EXPECT_EQ(edges.back().weight, values[i].weight);
        ASSERT_EQ(dag.get_edges(1).size(), 1u);
        EXPECT_EQ(dag.get_edges(1).front().next_pos, RuneIndex{2});
    }

    const auto stats = trie.collect_stats();
    EXPECT_EQ(stats.node_count, 3u);
    EXPECT_EQ(stats.direct_root_edge_count, 1u);
    EXPECT_EQ(stats.hashed_edge_count + stats.compact_edge_count, 3u);
    EXPECT_EQ(stats.value_count, 2u);
}

TEST(TrieCapacityTest, MatchesPrefixesAcrossSmallAndLargeChildCounts) {
    for (const auto fanout : std::array<size_t, 5>{1, 4, 5, 16, 64}) {
        auto keys = std::vector<Unicode>{};
        auto values = std::vector<DictUnit>{};
        keys.reserve(fanout * 4);
        values.reserve(fanout * 4);
        for (const auto root : std::array{U'中', U'𠮷'}) {
            for (auto child = size_t{0}; child < fanout; ++child) {
                const auto rune = static_cast<Rune>(0x10000 + child);
                keys.push_back(Unicode{root, rune});
                values.push_back(DictUnit{.weight = -static_cast<float>(keys.size())});
                keys.push_back(Unicode{root, rune, U'词'});
                values.push_back(DictUnit{.weight = -static_cast<float>(keys.size())});
            }
        }
        auto trie = Trie{};
        trie.build(keys, values);
        for (auto i = size_t{0}; i < keys.size(); ++i) {
            EXPECT_FLOAT_EQ(trie.find(keys[i]).weight, values[i].weight);
            if (keys[i].size() == 3) {
                auto ends = std::vector<RuneIndex>{};
                trie.for_each_match_from(keys[i], 0, [&](RuneIndex end, const DictUnit &word) {
                    ends.push_back(end);
                    EXPECT_FLOAT_EQ(word.weight, values[i - (end == 2)].weight);
                });
                EXPECT_EQ(ends, (std::vector<RuneIndex>{2, 3}));
            }
        }
        EXPECT_FALSE(trie.find(Unicode{U'中', static_cast<Rune>(0x10000 + fanout)}).has_value());
        EXPECT_FALSE(trie.find(Unicode{U'𠮷', static_cast<Rune>(0x10000 + fanout)}).has_value());
    }
}

TEST(TrieCapacityTest, ChildStorageSupportsMoreEdgesThanEightBitNodeIds) {
    auto keys = std::vector<Unicode>{};
    auto values = std::vector<DictUnit>{};
    keys.reserve(320);
    values.reserve(320);
    for (auto parent = size_t{0}; parent < 80; ++parent) {
        for (auto child = size_t{0}; child < 4; ++child) {
            keys.push_back(Unicode{static_cast<Rune>(U'中' + parent), static_cast<Rune>(0x10000 + child)});
            values.push_back(DictUnit{.weight = -static_cast<float>(keys.size())});
        }
    }
    auto trie = Trie{};
    trie.build(keys, values);
    for (auto i = size_t{0}; i < keys.size(); ++i) {
        EXPECT_FLOAT_EQ(trie.find(keys[i]).weight, values[i].weight);
    }
    EXPECT_EQ(trie.node_count(), 81u);
    const auto stats = trie.collect_stats();
    EXPECT_EQ(stats.value_count, 320u);
    EXPECT_EQ(stats.compact_edge_count, 320u);
    EXPECT_EQ(stats.edge_count, 400u);
}
