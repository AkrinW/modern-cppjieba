#pragma once

#include "neo/Config.hpp"
#include "neo/Traits.hpp"
#include "neo/Unicode.hpp"
#include "neo/detail/Dag.hpp"
#include "neo/detail/Logging.hpp"
#include "neo/detail/PosTag.hpp"
#include "neo/detail/TrieStats.hpp"
#include "neo/third_party/gtl.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace neo_cppjieba {

namespace detail {

/// A transition retains the full parent id and rune independently of the configured node width.
template <CapacityInteger NodeId>
struct TrieTransitionKey {
    NodeId parent;
    Rune rune;

    auto operator==(const TrieTransitionKey &) const noexcept -> bool = default;
};

/// Narrow ids retain the packed-key hash; wider ids contribute all 64-bit chunks to the hash.
struct TrieTransitionHash {
    template <CapacityInteger NodeId>
    auto operator()(const TrieTransitionKey<NodeId> &key) const noexcept -> std::size_t {
        const auto rune = static_cast<std::uint32_t>(key.rune);
        if constexpr (sizeof(NodeId) <= sizeof(std::uint32_t)) {
            const auto packed = (static_cast<std::uint64_t>(key.parent) << 32) | rune;
            return gtl::Hash<std::uint64_t>{}(packed);
        } else if constexpr (sizeof(NodeId) <= sizeof(std::uint64_t)) {
            return gtl::HashState::combine(0, key.parent, rune);
        } else {
            return gtl::HashState::combine(0, static_cast<std::uint64_t>(key.parent),
                                           static_cast<std::uint64_t>(key.parent >> 64), rune);
        }
    }
};

} // namespace detail

/// Compact dictionary entry — 8 bytes total, stored by value in trie nodes (no pointer indirection).
///
/// Replacing the original DictUnit { Unicode word; double weight; string tag; } (~80 bytes)
/// with a minimal { float weight; PosTag tag; } that fits in 8 bytes — same size as a pointer
/// on 64-bit platforms — eliminates one level of indirection entirely.
struct DictUnit {
    float weight{kMissingWordWeight};
    PosTag tag{};

    /// A valid dictionary entry always has a non-zero weight.
    /// Default-constructed (weight == 0) means "no value".
    /// These former rules are superseded: zero is valid; only kMissingWordWeight means "no value".
    [[nodiscard]] constexpr auto has_value() const noexcept -> bool {
        return weight != kMissingWordWeight;
    }
};

static_assert(sizeof(DictUnit) == 8, "DictUnit must be exactly 8 bytes");
static_assert(alignof(DictUnit) == 4);

/// A read-only character trie with direct BMP root transitions and one flat hash table.
/// Each transition carries its dictionary value and a filter for the child's outgoing runes.
class Trie {
    static constexpr auto kAbsentNode = std::numeric_limits<TrieNodeId>::max();

    /// The maximum child id marks an absent root slot; zero marks a terminal edge without a child node.
    struct Entry {
        TrieNodeId child{kAbsentNode};
        uint32_t filter{0};
        DictUnit value{};
    };
    static_assert(sizeof(TrieNodeId) > sizeof(std::uint32_t) || sizeof(Entry) == 16);

    using TransitionKey = detail::TrieTransitionKey<TrieNodeId>;
    using Transitions = gtl::flat_hash_map<TransitionKey, Entry, detail::TrieTransitionHash>;
    static constexpr size_t kRootTableLimit = 0x10000;

    std::vector<Entry> root_;
    Transitions transitions_;
    size_t node_count_{0};

    /// Keep all rune bits separate from the parent id; root transitions use parent id zero.
    [[nodiscard]] static constexpr auto transition_key(TrieNodeId parent, Rune rune) noexcept -> TransitionKey {
        assert_check([&] { return parent != kAbsentNode; }, "Trie: absent transition parent");
        return {parent, rune};
    }

    /// Collisions only add a table lookup; both 16-bit halves must match before probing.
    [[nodiscard]] static constexpr auto child_bits(Rune rune) noexcept -> uint32_t {
        const uint32_t hash = static_cast<uint32_t>(rune) * uint32_t{0x9E3779B1};
        return (uint32_t{1} << (hash >> 28)) | (uint32_t{1} << (16 + ((hash >> 24) & 15)));
    }

    [[nodiscard]] auto find_root(Rune rune) const noexcept -> const Entry * {
        assert_check([this] { return node_count_ > 0; }, "Trie: root lookup on an empty trie");
        if (rune < root_.size()) {
            const auto &entry = root_[rune];
            return entry.child != kAbsentNode ? &entry : nullptr;
        }
        const auto it = transitions_.find(transition_key(0, rune));
        return it != transitions_.end() ? &it->second : nullptr;
    }

    [[nodiscard]] auto find_child(const Entry &parent, Rune rune) const noexcept -> const Entry * {
        assert_check([&] { return parent.child != kAbsentNode && parent.child < node_count_; },
                     "Trie: invalid transition parent");
        const auto bits = child_bits(rune);
        if ((parent.filter & bits) != bits) {
            assert_check(
                [&] { return parent.child == 0 || !transitions_.contains(transition_key(parent.child, rune)); },
                "Trie: child filter rejected an existing rune");
            return nullptr;
        }
        assert_check([&] { return parent.child > 0; }, "Trie: terminal edge has a nonempty child filter");
        const auto it = transitions_.find(transition_key(parent.child, rune));
        return it != transitions_.end() ? &it->second : nullptr;
    }

    /// The returned reference is consumed before the next insertion, which may rehash the table.
    auto ensure_transition(TrieNodeId parent, Rune rune) -> Entry & {
        if (parent == 0 && rune < kRootTableLimit) {
            assert_check([&] { return rune < root_.size(); }, "Trie: root table does not cover a dictionary rune");
            auto &entry = root_[rune];
            if (entry.child == kAbsentNode) {
                entry.child = 0;
            }
            return entry;
        }
        return transitions_.try_emplace(transition_key(parent, rune), Entry{0, 0, {}}).first->second;
    }

    auto insert_word(std::span<const Rune> key, DictUnit value, std::vector<uint32_t> &filters) -> void {
        auto parent = TrieNodeId{0};
        for (size_t i = 0; i < key.size(); ++i) {
            assert_check([&] { return parent != kAbsentNode && parent < filters.size(); },
                         "Trie: invalid build node index");
            auto &entry = ensure_transition(parent, key[i]);
            filters[static_cast<size_t>(parent)] |= child_bits(key[i]);
            if (i + 1 == key.size()) {
                // Superseded by the last-value-wins policy; only the value is replaced.
                // assert_check([&] { return !it->second.value.has_value(); },
                //              "Trie: duplicate dictionary key"); // duplicate key should not have a value already
                entry.value = value;
                return;
            }
            if (entry.child == 0) {
                // Lazy materialization: allocate a child node now.
                assert_check([&] { return filters.size() < kAbsentNode; },
                             "Trie: node count exceeds the configured index range");
                entry.child = static_cast<TrieNodeId>(filters.size());
                filters.push_back(0);
            }
            parent = entry.child;
        }
    }

    auto build_storage(std::span<const Unicode> keys, std::span<const DictUnit> values) -> void {
        if (keys.empty()) {
            return;
        }
        size_t root_extent = 0;
        size_t word_count = 0;
        for (const auto &key : keys) {
            if (key.empty()) {
                continue;
            }
            ++word_count;
            if (key.front() < kRootTableLimit) {
                root_extent = std::max(root_extent, static_cast<size_t>(key.front()) + 1);
            }
        }
        root_.resize(root_extent);
        transitions_.reserve(word_count);
        auto filters = std::vector<uint32_t>{0};
        assert_check([&] { return word_count <= filters.max_size(); },
                     "Trie: too many dictionary words to reserve build nodes");
        filters.reserve(word_count);
        for (size_t i = 0; i < keys.size(); ++i) {
            insert_word(keys[i], values[i], filters);
        }
        const auto set_filter = [&](Entry &entry) {
            assert_check([&] { return entry.child == kAbsentNode || entry.child < filters.size(); },
                         "Trie: invalid built child index");
            if (entry.child != kAbsentNode && entry.child > 0) {
                entry.filter = filters[static_cast<size_t>(entry.child)];
                assert_check([&] { return entry.filter != 0; }, "Trie: materialized node has no children");
            }
        };
        for (auto &entry : root_) {
            set_filter(entry);
        }
        for (auto &[key, entry] : transitions_) {
            set_filter(entry);
        }
        node_count_ = filters.size();
    }

public:
    explicit Trie() = default;
    Trie(const Trie &) = delete;
    auto operator=(const Trie &) -> Trie & = delete;

    Trie(Trie &&other) noexcept
        : root_{std::move(other.root_)}, transitions_{std::move(other.transitions_)},
          node_count_{std::exchange(other.node_count_, 0)} {
    }

    auto operator=(Trie &&other) noexcept -> Trie & {
        if (this != &other) {
            root_ = std::move(other.root_);
            transitions_ = std::move(other.transitions_);
            node_count_ = std::exchange(other.node_count_, 0);
        }
        return *this;
    }

    /// Build from parallel key/value arrays. Empty keys are skipped; duplicate keys use the last value.
    /// Publish only a complete trie, so allocation or size failures leave the previous dictionary intact.
    auto build(std::span<const Unicode> keys, std::span<const DictUnit> values) -> void {
        assert_check([&] { return keys.size() == values.size(); }, "Trie: keys and values must have equal sizes");
        auto next = Trie{};
        next.build_storage(keys, values);
        *this = std::move(next);
    }

    /// Find an exact key, returning its dictionary payload by value.
    [[nodiscard]] auto find(std::span<const Rune> key) const -> DictUnit {
        if (empty() || key.empty()) {
            return {};
        }
        const auto *entry = find_root(key.front());
        for (size_t i = 1; entry && i < key.size(); ++i) {
            entry = find_child(*entry, key[i]);
        }
        return entry ? entry->value : DictUnit{};
    }

    /// Convenience overload: decode any StringLike input (UTF-8, UTF-16, …) then look up.
    template <StringLike T>
    [[nodiscard]] auto find(const T &input) const -> DictUnit {
        const auto unicode = decode(input);
        return find(std::span<const Rune>{unicode});
    }

    /// Find all matching prefixes in the trie to build a flat compressed DAG.
    /// Returns a Dag containing offsets and edges.
    [[nodiscard]] auto find_dag(std::span<const Rune> sentence) const -> Dag {
        auto dag = Dag{};
        find_dag_into(sentence, dag);
        return dag;
    }

    // Replace the DAG while retaining storage for subsequent segments and calls.
    auto find_dag_into(std::span<const Rune> sentence, Dag &dag) const -> void {
        const auto n = sentence.size();
        assert_check(
            [&] { return n <= std::numeric_limits<RuneIndex>::max() && n < std::numeric_limits<size_t>::max(); },
            "Trie: sentence has {} runes, exceeding the supported DAG index range", n);
        dag.offsets.clear();
        dag.edges.clear();
        dag.offsets.reserve(n + 1);
        if (empty() || n == 0) {
            dag.offsets.resize(n + 1);
            return;
        }

        // Every rune contributes a single-rune edge, even when no dictionary word matches.
        dag.edges.reserve(n);

        // We accumulate the edges directly into a flat vector.
        for (size_t i = 0; i < n; ++i) {
            assert_check([&] { return dag.edges.size() <= std::numeric_limits<DagOffset>::max(); },
                         "Trie: DAG edge count exceeds the supported offset range");
            dag.offsets.push_back(static_cast<DagOffset>(dag.edges.size()));

            // In typical jieba, an edge for length-1 (single character) is always added first,
            // even if it does not form a word (weight = 0.0f).
            // The former zero sentinel is now kMissingWordWeight; known zero weights are retained.
            const auto *entry = find_root(sentence[i]);
            dag.edges.push_back({static_cast<RuneIndex>(i + 1), entry ? entry->value.weight : kMissingWordWeight});
            for (size_t j = i + 1; entry && j < n; ++j) {
                entry = find_child(*entry, sentence[j]);
                if (entry && entry->value.has_value()) {
                    dag.edges.push_back({static_cast<RuneIndex>(j + 1), entry->value.weight});
                }
            }
        }
        assert_check([&] { return dag.edges.size() <= std::numeric_limits<DagOffset>::max(); },
                     "Trie: DAG edge count exceeds the supported offset range");
        dag.offsets.push_back(static_cast<DagOffset>(dag.edges.size()));
    }

    template <StringLike T>
    [[nodiscard]] auto find_dag(const T &input) const -> Dag {
        const auto unicode = decode(input);
        return find_dag(std::span<const Rune>{unicode});
    }

    /// Return the total number of nodes in the trie (including the root).
    [[nodiscard]] auto node_count() const noexcept -> size_t {
        return node_count_;
    }

    /// Check whether the trie is empty (has not been built, or built with no keys).
    [[nodiscard]] auto empty() const noexcept -> bool {
        return node_count_ == 0;
    }

    /// Reconstruct parent/depth information only when statistics are requested.
    /// Parent ids precede child ids, so a linear pass suffices without retained node objects.
    [[nodiscard]] auto collect_stats() const -> TrieStats {
        auto stats = TrieStats{};
        stats.node_count = node_count_;
        if (empty()) {
            return stats;
        }

        /// Temporary metadata for the cold statistics traversal.
        struct NodeInfo {
            size_t depth{0};
            size_t fanout{0};
            TrieNodeId parent{kAbsentNode};
        };
        auto nodes = std::vector<NodeInfo>(node_count_);
        const auto visit = [&](TrieNodeId parent, const Entry &entry) {
            assert_check([&] { return parent != kAbsentNode && parent < node_count_; },
                         "Trie: invalid statistics parent");
            assert_check([&] { return entry.child != kAbsentNode; }, "Trie: absent transition in statistics");
            ++nodes[static_cast<size_t>(parent)].fanout;
            ++stats.edge_count;
            stats.value_count += entry.value.has_value();
            if (entry.child == 0) {
                ++stats.lazy_edge_count;
                return;
            }
            assert_check([&] { return parent < entry.child && entry.child < node_count_; },
                         "Trie: child ids must follow their parents");
            auto &child = nodes[static_cast<size_t>(entry.child)];
            assert_check([&] { return child.parent == kAbsentNode; }, "Trie: statistics revisited a node");
            child.parent = parent;
        };
        for (const auto &entry : root_) {
            if (entry.child != kAbsentNode) {
                visit(0, entry);
                ++stats.direct_root_edge_count;
            }
        }
        for (const auto &[key, entry] : transitions_) {
            visit(key.parent, entry);
        }
        stats.hashed_edge_count = transitions_.size();

        size_t total_depth = 0;
        size_t total_leaf_depth = 0;
        for (size_t i = 0; i < nodes.size(); ++i) {
            auto &node = nodes[i];
            if (i > 0) {
                assert_check([&] { return node.parent != kAbsentNode && node.parent < i; },
                             "Trie: unreachable statistics node");
                node.depth = nodes[static_cast<size_t>(node.parent)].depth + 1;
            }
            total_depth += node.depth;
            if (node.fanout == 0) {
                ++stats.leaf_count;
                total_leaf_depth += node.depth;
            }
            stats.max_depth = std::max(stats.max_depth, node.depth);
            stats.max_fanout = std::max(stats.max_fanout, node.fanout);
            if (node.depth >= stats.depth_histogram.size()) {
                stats.depth_histogram.resize(node.depth + 1, 0);
            }
            ++stats.depth_histogram[node.depth];
            if (node.fanout >= stats.fanout_histogram.size()) {
                stats.fanout_histogram.resize(node.fanout + 1, 0);
            }
            ++stats.fanout_histogram[node.fanout];
        }
        stats.avg_depth = static_cast<double>(total_depth) / static_cast<double>(node_count_);
        if (stats.leaf_count > 0) {
            stats.avg_leaf_depth = static_cast<double>(total_leaf_depth) / static_cast<double>(stats.leaf_count);
        }
        const auto non_leaf_count = node_count_ - stats.leaf_count;
        if (non_leaf_count > 0) {
            stats.avg_fanout = static_cast<double>(stats.edge_count) / static_cast<double>(non_leaf_count);
        }
        stats.transition_capacity = transitions_.capacity();
        stats.transition_load_factor = transitions_.load_factor();
        stats.root_table_bytes = root_.capacity() * sizeof(Entry);
        if (stats.transition_capacity > 0) {
            // Include slot payloads, control bytes, and an upper bound for SIMD/padding overhead.
            stats.transition_table_bytes = stats.transition_capacity * (sizeof(Transitions::value_type) + 1) + 32;
        }
        stats.total_estimated_bytes = stats.root_table_bytes + stats.transition_table_bytes;
        return stats;
    }
};

} // namespace neo_cppjieba
