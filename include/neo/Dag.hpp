#pragma once

#include "StringUtil.hpp"
#include "Unicode.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace neo_cppjieba {

struct DagEdge {
    uint32_t next_pos; // The index of the next rune after the matched word
    float weight;
};

/// A flat compressed Directed Acyclic Graph (DAG) using a CSR-like format.
///
/// For a sentence of N runes, `offsets` has N+1 elements.
/// `offsets[i] .. offsets[i+1]` is the half-open range in `edges` for vertex i.
struct Dag {
    std::vector<uint32_t> offsets;
    std::vector<DagEdge> edges;

    /// Return the outgoing edges from vertex `i`.
    [[nodiscard]] auto get_edges(size_t i) const -> std::span<const DagEdge> {
        if (i + 1 >= offsets.size()) {
            return {};
        }
        return {edges.data() + offsets[i], edges.data() + offsets[i + 1]};
    }

    /// Number of rune positions in this DAG (i.e. sentence length).
    [[nodiscard]] auto size() const noexcept -> size_t {
        return offsets.empty() ? 0 : offsets.size() - 1;
    }

    /// Dump all sub-words and their weights as a human-readable UTF-8 string.
    ///
    /// For each position i, outputs every candidate sub-word with its weight.
    /// Format per entry: "word(weight)" separated by `sep`.
    ///
    /// @param runes  the original Unicode sequence (must match DAG size)
    /// @param sep    separator between entries (default: ", ")
    [[nodiscard]] auto to_string(std::span<const Rune> runes, std::string_view sep = ", ") const -> std::string {
        auto n = size();
        if (n == 0 || runes.size() != n) {
            return {};
        }

        auto result = std::string{};
        result.reserve(n * 12); // rough estimate

        auto first = true;
        for (auto i = size_t{0}; i < n; ++i) {
            for (auto &&edge : get_edges(i)) {
                if (!first) {
                    result.append(sep);
                }
                first = false;
                result.append(encode(runes.subspan(i, edge.next_pos - i)));
                result.push_back('(');
                result.append(encode_value(edge.weight));
                result.push_back(')');
            }
        }

        return result;
    }
};

} // namespace neo_cppjieba
