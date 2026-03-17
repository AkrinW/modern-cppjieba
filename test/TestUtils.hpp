#pragma once

/// Shared utilities for neo unit tests:
///   - to_strings : convert WordRange vector to UTF-8 string vector
///   - join       : concatenate strings with a separator

#include "neo/Unicode.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/// Convert a vector of WordRange to a vector of UTF-8 strings for easy comparison.
inline auto to_strings(std::span<const neo_cppjieba::Rune> runes, const std::vector<neo_cppjieba::WordRange> &ranges)
    -> std::vector<std::string> {
    auto result = std::vector<std::string>{};
    result.reserve(ranges.size());
    for (auto &&r : ranges) {
        result.push_back(neo_cppjieba::encode(runes.subspan(r.begin, r.size())));
    }
    return result;
}

/// Join a vector of strings with a separator for display.
inline auto join(const std::vector<std::string> &v, std::string_view sep = "/") -> std::string {
    auto result = std::string{};
    for (auto i = size_t{0}; i < v.size(); ++i) {
        if (i > 0) {
            result.append(sep);
        }
        result.append(v[i]);
    }
    return result;
}
