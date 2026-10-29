#pragma once

/// Shared helpers for libFuzzer targets:
///   - require : abort when a harness property fails, so libFuzzer keeps the input
///   - as_text : view fuzz bytes as UTF-8 text without copying

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

// Abort with a message so libFuzzer records the input that broke a harness property.
inline auto require(bool condition, std::string_view property) -> void {
    if (!condition) {
        std::fprintf(stderr, "fuzz property violated: %.*s\n", static_cast<int>(property.size()), property.data());
        std::abort();
    }
}

// View fuzz bytes as UTF-8 text; libFuzzer keeps them alive for the whole callback.
inline auto as_text(const std::uint8_t *data, std::size_t size) -> std::string_view {
    return size == 0 ? std::string_view{} : std::string_view{reinterpret_cast<const char *>(data), size};
}
