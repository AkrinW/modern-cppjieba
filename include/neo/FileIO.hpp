#pragma once

#include "Logging.hpp"

#include <cstddef>
#include <fcntl.h>
#include <string_view>
#include <unistd.h>
#include <utility>

#include <sys/mman.h>
#include <sys/stat.h>

namespace neo_cppjieba {

// MappedFile provides a zero-copy, RAII wrapper around mmap(2) for read-only file access.
//
// For dictionary files that are read sequentially once (e.g. jieba.dict.utf8 at ~5MB / 350K lines),
// memory-mapping avoids the overhead of ifstream/getline (per-line allocation, copying from kernel
// buffer to user-space std::string, etc.). The mapped region is accessed via a single std::string_view,
// and downstream consumers (lines_view, split_view) produce further string_views that point directly
// into the mapped memory — achieving true zero-copy parsing.
//
// MADV_SEQUENTIAL is applied so the kernel aggressively reads ahead and can free pages behind the
// access cursor, which is optimal for a single sequential scan.
//
// Usage:
//   auto file = MappedFile("dict/jieba.dict.utf8");
//   auto content = file.content();  // std::string_view over the entire file
//
class MappedFile {
public:
    explicit MappedFile(std::string_view path) {
        // open(2) requires a null-terminated path
        auto null_terminated_path = std::string{path};
        fd_ = ::open(null_terminated_path.c_str(), O_RDONLY);

        check(fd_ != -1, "MappedFile: failed to open file: {}", path);

        struct ::stat st{};
        check(::fstat(fd_, &st) == 0, "MappedFile: failed to stat file: {}", path);
        size_ = static_cast<size_t>(st.st_size);

        if (size_ == 0) {
            data_ = nullptr;
            return;
        }

        auto *ptr = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
        check(ptr != MAP_FAILED, "MappedFile: mmap failed for file: {}", path);
        data_ = static_cast<const char *>(ptr);

        // Hint the kernel: we will read sequentially from start to end, once.
        ::madvise(const_cast<void *>(static_cast<const void *>(data_)), size_, MADV_SEQUENTIAL);
    }

    ~MappedFile() {
        if (data_) {
            ::munmap(const_cast<void *>(static_cast<const void *>(data_)), size_);
        }
        if (fd_ != -1) {
            ::close(fd_);
        }
    }

    // Move-only: transferring ownership of the mapping.
    MappedFile(MappedFile &&other) noexcept
        : data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0)),
          fd_(std::exchange(other.fd_, -1)) {
    }

    auto operator=(MappedFile &&other) noexcept -> MappedFile & {
        if (this != &other) {
            if (data_) {
                ::munmap(const_cast<void *>(static_cast<const void *>(data_)), size_);
            }
            if (fd_ != -1) {
                ::close(fd_);
            }
            data_ = std::exchange(other.data_, nullptr);
            size_ = std::exchange(other.size_, 0);
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    MappedFile(const MappedFile &) = delete;
    auto operator=(const MappedFile &) -> MappedFile & = delete;

    // content returns a string_view spanning the entire mapped file.
    // The view is valid for the lifetime of this MappedFile object.
    [[nodiscard]] auto content() const noexcept -> std::string_view {
        return std::string_view{data_, size_};
    }

    // size returns the file size in bytes.
    [[nodiscard]] auto size() const noexcept -> size_t {
        return size_;
    }

    // is_open returns true if the underlying file descriptor is valid.
    [[nodiscard]] auto is_open() const noexcept -> bool {
        return fd_ != -1;
    }

private:
    const char *data_{nullptr};
    size_t size_{0};
    int fd_{-1};
};

// get_map_file memory-maps the given file path and returns a MappedFile.
inline auto get_map_file(std::string_view path) -> MappedFile {
    return MappedFile{path};
}

} // namespace neo_cppjieba
