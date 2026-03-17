#pragma once

/// Shared utilities for benchmark programs:
///   - DoNotOptimize  : compiler barrier to prevent dead-code elimination
///   - Clock / Ms     : timing type aliases
///   - load_lines     : mmap-based UTF-8 line loader

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

#include <sys/mman.h>
#include <sys/stat.h>

// ─────────────────────────────────────────────────────────────────────────────
// Anti-optimization barrier (like Google Benchmark's DoNotOptimize)
// ─────────────────────────────────────────────────────────────────────────────

template <typename T>
inline auto DoNotOptimize(const T &value) -> void {
    asm volatile("" : : "r,m"(value) : "memory");
}

template <typename T>
inline auto DoNotOptimize(T &value) -> void {
    asm volatile("" : "+m"(value) : : "memory");
}

// ─────────────────────────────────────────────────────────────────────────────
// Timing helpers
// ─────────────────────────────────────────────────────────────────────────────

using Clock = std::chrono::high_resolution_clock;
using Ms = std::chrono::duration<double, std::milli>;

// ─────────────────────────────────────────────────────────────────────────────
// Load lines from a UTF-8 text file via mmap
// ─────────────────────────────────────────────────────────────────────────────

inline auto load_lines(const std::string &path) -> std::vector<std::string> {
    auto lines = std::vector<std::string>{};

    auto fd = ::open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        std::perror("open");
        return lines;
    }
    struct stat st {};
    ::fstat(fd, &st);
    auto file_size = static_cast<size_t>(st.st_size);
    const auto *data = static_cast<const char *>(::mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (data == MAP_FAILED) {
        ::close(fd);
        std::perror("mmap");
        return lines;
    }

    const auto *p = data;
    const auto *end = data + file_size;
    while (p < end) {
        const auto *line_end = static_cast<const char *>(std::memchr(p, '\n', end - p));
        if (!line_end) {
            line_end = end;
        }
        auto line = std::string_view(p, line_end - p);
        p = line_end + 1;
        if (line.empty()) {
            continue;
        }
        if (line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (!line.empty()) {
            lines.emplace_back(line);
        }
    }

    ::munmap(const_cast<void *>(static_cast<const void *>(data)), file_size);
    ::close(fd);
    return lines;
}
