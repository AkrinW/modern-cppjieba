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
        values.push_back(DictUnit{-static_cast<float>(i), {}});
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
    EXPECT_EQ(stats.hashed_edge_count, 254u);
    EXPECT_EQ(stats.value_count, 254u);
    EXPECT_EQ(stats.max_depth, 1u);
}

TEST(TrieCapacityTest, DirectAndHashedRootsPreserveDagMatches) {
    const auto keys = std::array{Unicode{U'甲', U'𠮷'}, Unicode{U'𠮷', U'甲'}};
    const auto values = std::array{DictUnit{-1.0f, {}}, DictUnit{-2.0f, {}}};
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
    EXPECT_EQ(stats.hashed_edge_count, 3u);
    EXPECT_EQ(stats.value_count, 2u);
}
