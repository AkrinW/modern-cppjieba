#pragma once

#include "neo/Traits.hpp"

#include "Dag.hpp"
#include "PosTag.hpp"
#include "TrieStats.hpp"
#include "Unicode.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <queue>
#include <span>
#include <unordered_map>
#include <vector>

namespace neo_cppjieba {

/// Compact dictionary entry — 8 bytes total, stored by value in trie nodes (no pointer indirection).
///
/// Replacing the original DictUnit { Unicode word; double weight; string tag; } (~80 bytes)
/// with a minimal { float weight; PosTag tag; } that fits in 8 bytes — same size as a pointer
/// on 64-bit platforms — eliminates one level of indirection entirely.
struct DictUnit {
    float weight{0.0};
    PosTag tag{};

    /// A valid dictionary entry always has a non-zero weight.
    /// Default-constructed (weight == 0) means "no value".
    [[nodiscard]] constexpr auto has_value() const noexcept -> bool {
        return weight != 0.0f;
    }
};

static_assert(sizeof(DictUnit) == 8, "DictUnit must be exactly 8 bytes");
static_assert(alignof(DictUnit) == 4);

/// A compact Trie with contiguous node layout and adaptive child storage.
///
/// Design goals:
///   1. All TrieNode objects live in a single flat std::vector<Node> — no per-node
///      heap allocation for the nodes themselves. This gives good spatial locality
///      when traversing from parent to child, especially with BFS-ordered indices.
///   2. Child storage adapts to fanout:
///        • Flat mode (≤ kFlatThreshold children): a sorted std::vector<ChildPair>
///          is scanned linearly — cache-friendly and avoids hash overhead.
///        • Map mode  (> kFlatThreshold children): std::unordered_map<Rune, ChildEntry>
///          for O(1) amortized lookup on high-fanout nodes (e.g. the root).
///      The threshold is chosen so that ~99 % of nodes use the flat path.
///   3. DictUnit is stored inline in the ChildEntry (on the parent's edge),
///      so the final lookup step reads the value directly — no extra node deref.
///      A valid entry is distinguished by `DictUnit::has_value()` (weight > 0).
///   4. Nodes are BFS-ordered so that shallow levels (frequently accessed) cluster
///      at the front of the vector.
///
class Trie {
    /// Payload stored per edge. Carries the child index and an inline DictUnit
    /// so that find() can return the value without dereferencing the child node.
    struct ChildEntry {
        int32_t child_index{-1}; // index of the child node in the nodes_ vector; -1 means no child
        DictUnit value{};
    };

    /// A (rune, entry) pair used in the flat-array storage path.
    struct ChildPair {
        Rune rune{};
        ChildEntry entry{};
    };

    /// Fanout threshold below which a node stores children in an inline array
    /// (linear scan, zero heap allocation) rather than an unordered_map (hash lookup).
    /// From jieba.dict stats: ~99 % of nodes have fanout ≤ 8.
    static constexpr auto kFlatThreshold = size_t{3};

    /// Inline fixed-capacity array of ChildPair — replaces std::vector to avoid
    /// per-node heap allocation for the ~99 % of nodes with low fanout.
    /// sizeof = kFlatThreshold * sizeof(ChildPair) + padding ≈ 132 bytes.
    struct FlatChildren {
        std::array<ChildPair, kFlatThreshold> data_{};
        uint8_t size_{0};

        auto push_back(const ChildPair &p) noexcept -> void {
            assert(size_ < kFlatThreshold);
            data_[size_] = p;
            ++size_;
        }
        [[nodiscard]] auto size() const noexcept -> size_t {
            return size_;
        }
        [[nodiscard]] auto begin() const noexcept -> const ChildPair * {
            return data_.data();
        }
        [[nodiscard]] auto end() const noexcept -> const ChildPair * {
            return data_.data() + size_;
        }
    };

    using MapChildren = std::unordered_map<Rune, ChildEntry>;

    struct Node {
        enum class Kind : uint8_t { Flat, Map };

        union {
            FlatChildren flat;
            MapChildren map;
        };
        Kind kind;

        explicit Node() noexcept : flat{}, kind{Kind::Flat} {
        }

        ~Node() {
            if (kind == Kind::Map) {
                map.~MapChildren();
            }
        }

        Node(Node &&o) noexcept : kind{o.kind} {
            if (kind == Kind::Flat) {
                flat = o.flat;
            } else {
                std::construct_at(&map, std::move(o.map));
            }
        }

        auto operator=(Node &&o) noexcept -> Node & {
            if (this != &o) {
                if (kind == Kind::Map) {
                    map.~MapChildren();
                }
                kind = o.kind;
                if (kind == Kind::Flat) {
                    flat = o.flat;
                } else {
                    std::construct_at(&map, std::move(o.map));
                }
            }
            return *this;
        }

        Node(const Node &) = delete;
        auto operator=(const Node &) -> Node & = delete;

        auto set_flat(const FlatChildren &f) noexcept -> void {
            if (kind == Kind::Map) {
                map.~MapChildren();
            }
            flat = f;
            kind = Kind::Flat;
        }

        auto set_map(MapChildren &&m) noexcept -> void {
            if (kind == Kind::Map) {
                map.~MapChildren();
            }
            std::construct_at(&map, std::move(m));
            kind = Kind::Map;
        }

        /// Look up a child by rune. Returns nullptr if not found.
        [[nodiscard]] auto find_child(Rune r) const noexcept -> const ChildEntry * {
            if (kind == Kind::Flat) {
                for (const auto &p : flat) {
                    if (p.rune == r) {
                        return &p.entry;
                    }
                }
                return nullptr;
            }
            if (auto it = map.find(r); it != map.end()) {
                return &it->second;
            }
            return nullptr;
        }

        /// Number of children.
        [[nodiscard]] auto child_count() const noexcept -> size_t {
            return kind == Kind::Flat ? flat.size() : map.size();
        }

        /// Check if this node uses flat storage.
        [[nodiscard]] auto is_flat() const noexcept -> bool {
            return kind == Kind::Flat;
        }
    };

    std::vector<Node> nodes_;

public:
    explicit Trie() = default;

    /// Build the trie from parallel arrays of keys and values.
    ///
    /// Each key is a Unicode sequence (vector<Rune>); each value is a DictUnit.
    /// Empty keys are silently skipped. Duplicate keys: last value wins.
    ///
    /// The internal layout is produced in three phases:
    ///   1. Insert all keys into a temporary dynamic tree (vector-based, indexed).
    ///      The DictUnit is stored on the *edge leading to* the terminal node,
    ///      i.e. in the parent's map entry for the last Rune.
    ///   2. BFS over the tree to assign contiguous indices (improves cache locality).
    ///   3. Copy into the flat nodes_ vector with remapped child indices,
    ///      choosing flat-array or hash-map storage per node based on fanout.
    auto build(std::span<const Unicode> keys, std::span<const DictUnit> values) -> void {
        assert(keys.size() == values.size());
        nodes_.clear();

        if (keys.empty()) {
            return;
        }

        // ── Phase 1: Build a temporary tree with lazy node allocation ────
        //
        // Temporary nodes always use unordered_map for efficient insertion.
        // An edge's child_index is -1 when no child node has been needed yet.
        // A child node is only materialized when a subsequent key must traverse
        // through that edge, keeping the total node count minimal.

        auto temp = std::vector<MapChildren>{};
        temp.reserve(keys.size() * 2); // rough estimate
        temp.emplace_back();           // root at index 0

        for (auto i = size_t{0}; i < keys.size(); ++i) {
            if (keys[i].empty()) {
                continue;
            }

            auto cur = int32_t{0};

            // Traverse / insert all runes except the last — these need child nodes.
            for (auto j = size_t{0}; j + 1 < keys[i].size(); ++j) {
                auto &&r = keys[i][j];
                auto &map = temp[cur];
                auto it = map.find(r);
                if (it == map.end()) {
                    cur = static_cast<int32_t>(temp.size());
                    map.emplace(r, ChildEntry{cur});
                    temp.emplace_back();
                } else {
                    if (it->second.child_index < 0) {
                        // Lazy materialization: allocate a child node now.
                        it->second.child_index = static_cast<int32_t>(temp.size());
                        temp.emplace_back();
                    }
                    cur = it->second.child_index;
                }
            }

            // Last rune: only need an edge with the value, no child node required.
            auto &&last_rune = keys[i].back();
            auto &map = temp[cur];
            auto it = map.find(last_rune);
            if (it == map.end()) {
                map.insert({last_rune, ChildEntry{-1, values[i]}});
            } else {
                assert(it->second.value.has_value() == false); // duplicate key should not have a value already
                it->second.value = values[i];
            }
        }

        // ── Phase 2: BFS to assign contiguous indices ────────────────────
        //
        // Only follow edges with child_index >= 0 (materialized child nodes).

        auto n = temp.size();
        auto bfs_order = std::vector<uint32_t>{};
        bfs_order.reserve(n);
        bfs_order.push_back(0);

        auto new_index = std::vector<int32_t>(n, -1);
        new_index[0] = 0;

        for (auto front = size_t{0}; front < bfs_order.size(); ++front) {
            for (auto &&[key, entry] : temp[bfs_order[front]]) {
                if (entry.child_index >= 0) {
                    new_index[entry.child_index] = static_cast<int32_t>(bfs_order.size());
                    bfs_order.push_back(static_cast<uint32_t>(entry.child_index));
                }
            }
        }

        // ── Phase 3: Populate flat nodes_ with remapped indices ──────────
        //
        // Per-node decision: if fanout ≤ kFlatThreshold → flat array,
        // otherwise → unordered_map.  ~99 % of nodes take the flat path.
        // Edges with child_index == -1 keep -1 (no remapping needed).

        nodes_.resize(n);
        for (auto i = size_t{0}; i < n; ++i) {
            auto &t = temp[bfs_order[i]];
            auto &node = nodes_[i];

            if (t.size() <= kFlatThreshold) {
                auto flat = FlatChildren{};
                for (auto &&[rune, old_entry] : t) {
                    auto new_child = old_entry.child_index >= 0 ? new_index[old_entry.child_index] : int32_t{-1};
                    flat.push_back(ChildPair{rune, ChildEntry{new_child, old_entry.value}});
                }
                node.set_flat(flat);
            } else {
                auto map = MapChildren{};
                map.reserve(t.size() * 4);
                for (auto &&[rune, old_entry] : t) {
                    auto new_child = old_entry.child_index >= 0 ? new_index[old_entry.child_index] : int32_t{-1};
                    map.emplace(rune, ChildEntry{new_child, old_entry.value});
                }
                node.set_map(std::move(map));
            }
        }
    }

    /// Find the DictUnit for an exact key match.
    ///
    /// @param key  a span of Runes representing the lookup key
    /// @return     pointer to the stored DictUnit if found; nullptr otherwise.
    ///             The pointer remains valid until the trie is rebuilt or destroyed.
    ///
    /// The value is fetched directly from the last ChildEntry in the map lookup,
    /// avoiding an extra nodes_[child_index] dereference for the terminal node.
    [[nodiscard]] auto find(std::span<const Rune> key) const -> DictUnit {
        if (nodes_.empty() || key.empty()) {
            return DictUnit{};
        }

        auto cur = int32_t{0}; // start at root
        const auto *last_entry = static_cast<const ChildEntry *>(nullptr);

        for (auto i = size_t{0}; i < key.size(); ++i) {
            if (cur < 0) {
                return DictUnit{}; // previous edge had no child node
            }
            last_entry = nodes_[cur].find_child(key[i]);
            if (!last_entry) {
                return DictUnit{};
            }
            cur = last_entry->child_index;
        }

        return last_entry->value;
    }

    /// Convenience overload: decode any StringLike input (UTF-8, UTF-16, …) then look up.
    template <StringLike T>
    [[nodiscard]] auto find(const T &input) const -> DictUnit {
        auto unicode = decode(input);
        return find(std::span<const Rune>{unicode});
    }

    /// Find all matching prefixes in the trie to build a flat compressed DAG.
    /// Returns a Dag containing offsets and edges.
    [[nodiscard]] auto find_dag(std::span<const Rune> sentence) const -> Dag {
        auto dag = Dag{};
        auto n = sentence.size();
        dag.offsets.resize(n + 1);

        if (nodes_.empty() || n == 0) {
            return dag;
        }

        // We accumulate the edges directly into a flat vector.
        for (auto i = size_t{0}; i < n; ++i) {
            dag.offsets[i] = static_cast<uint32_t>(dag.edges.size());
            auto cur = int32_t{0}; // start at root

            // In typical jieba, an edge for length-1 (single character) is always added first,
            // even if it does not form a word (weight = 0.0f).
            const auto *last_entry = nodes_[cur].find_child(sentence[i]);
            if (last_entry && last_entry->value.has_value()) {
                dag.edges.push_back(DagEdge{static_cast<uint32_t>(i + 1), last_entry->value.weight});
            } else {
                dag.edges.push_back(DagEdge{static_cast<uint32_t>(i + 1), 0.0f});
            }

            if (last_entry) {
                cur = last_entry->child_index;
                if (cur >= 0) {
                    for (auto j = i + 1; j < n; ++j) {
                        last_entry = nodes_[cur].find_child(sentence[j]);
                        if (!last_entry) {
                            break;
                        }
                        cur = last_entry->child_index;
                        if (last_entry->value.has_value()) {
                            dag.edges.push_back(DagEdge{static_cast<uint32_t>(j + 1), last_entry->value.weight});
                        }
                        if (cur < 0) {
                            break;
                        }
                    }
                }
            }
        }
        dag.offsets[n] = static_cast<uint32_t>(dag.edges.size());

        return dag;
    }

    template <StringLike T>
    [[nodiscard]] auto find_dag(const T &input) const -> Dag {
        auto unicode = decode(input);
        return find_dag(std::span<const Rune>{unicode});
    }

    /// Return the total number of nodes in the trie (including the root).
    [[nodiscard]] auto node_count() const noexcept -> size_t {
        return nodes_.size();
    }

    /// Check whether the trie is empty (has not been built, or built with no keys).
    [[nodiscard]] auto empty() const noexcept -> bool {
        return nodes_.empty();
    }

    /// Collect structural statistics of the trie.
    ///
    /// Performs a BFS traversal over the node vector, computing depth,
    /// branching factor, memory usage, and hash-map health metrics.
    [[nodiscard]] auto collect_stats() const -> TrieStats {
        auto stats = TrieStats{};
        auto n = nodes_.size();
        stats.node_count = n;

        if (n == 0) {
            return stats;
        }

        // ── BFS to compute depths ────────────────────────────────────────

        auto depth = std::vector<size_t>(n, 0);
        auto bfs = std::queue<uint32_t>{};
        bfs.push(0);

        size_t total_depth = 0;
        size_t total_leaf_depth = 0;

        while (!bfs.empty()) {
            auto cur = bfs.front();
            bfs.pop();

            const auto &node = nodes_[cur];
            auto fanout = node.child_count();

            stats.edge_count += fanout;

            // Helper lambda that processes each (rune, entry) pair.
            auto visit_child = [&](Rune /*rune*/, const ChildEntry &entry) {
                if (entry.value.has_value()) {
                    ++stats.value_count;
                }
                if (entry.child_index >= 0) {
                    auto ci = static_cast<uint32_t>(entry.child_index);
                    depth[ci] = depth[cur] + 1;
                    bfs.push(ci);
                } else {
                    ++stats.lazy_edge_count;
                }
            };

            if (node.is_flat()) {
                ++stats.flat_node_count;
                for (auto &&p : node.flat) {
                    visit_child(p.rune, p.entry);
                }
            } else {
                ++stats.map_node_count;
                for (auto &&[rune, entry] : node.map) {
                    visit_child(rune, entry);
                }
                if (!node.map.empty()) {
                    stats.avg_load_factor += node.map.load_factor();
                    stats.total_bucket_count += node.map.bucket_count();
                }
            }

            total_depth += depth[cur];

            if (fanout == 0) {
                ++stats.leaf_count;
                total_leaf_depth += depth[cur];
            }

            if (depth[cur] > stats.max_depth) {
                stats.max_depth = depth[cur];
            }
            if (fanout > stats.max_fanout) {
                stats.max_fanout = fanout;
            }

            if (depth[cur] >= stats.depth_histogram.size()) {
                stats.depth_histogram.resize(depth[cur] + 1, 0);
            }
            ++stats.depth_histogram[depth[cur]];

            if (fanout >= stats.fanout_histogram.size()) {
                stats.fanout_histogram.resize(fanout + 1, 0);
            }
            ++stats.fanout_histogram[fanout];
        }

        // ── Averages ─────────────────────────────────────────────────────

        stats.avg_depth = n > 0 ? static_cast<double>(total_depth) / static_cast<double>(n) : 0.0;
        stats.avg_leaf_depth =
            stats.leaf_count > 0 ? static_cast<double>(total_leaf_depth) / static_cast<double>(stats.leaf_count) : 0.0;

        size_t non_leaf = n - stats.leaf_count;
        stats.avg_fanout = non_leaf > 0 ? static_cast<double>(stats.edge_count) / static_cast<double>(non_leaf) : 0.0;
        stats.avg_load_factor =
            stats.map_node_count > 0 ? stats.avg_load_factor / static_cast<double>(stats.map_node_count) : 0.0;

        // ── Memory estimation ────────────────────────────────────────────
        constexpr auto node_struct_size = sizeof(Node);
        constexpr auto map_pair_size = sizeof(std::pair<const Rune, ChildEntry>);
        constexpr auto map_element_overhead = map_pair_size + 24;
        constexpr auto bucket_overhead = 8;

        stats.node_vector_bytes = node_struct_size * n;

        size_t map_edge_count = 0;
        for (size_t f = kFlatThreshold + 1; f < stats.fanout_histogram.size(); ++f) {
            map_edge_count += stats.fanout_histogram[f] * f;
        }

        size_t map_heap_bytes = map_edge_count * map_element_overhead + stats.total_bucket_count * bucket_overhead;

        stats.hashmap_overhead_bytes = map_heap_bytes;
        stats.total_estimated_bytes = stats.node_vector_bytes + stats.hashmap_overhead_bytes;

        return stats;
    }
};

} // namespace neo_cppjieba
