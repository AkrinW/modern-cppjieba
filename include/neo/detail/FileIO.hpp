#pragma once

#include "neo/detail/Logging.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>

#include <sys/stat.h>

namespace neo_cppjieba {

namespace detail {

// Fill owned storage, retrying interrupted reads and reporting I/O errors or premature EOF through check.
inline auto read_file_contents(int fd, std::span<char> destination, std::string_view path) -> void {
    assert_check([fd] { return fd >= 0; }, "FileBuffer: invalid internal file descriptor");

    auto offset = size_t{0};
    while (offset < destination.size()) {
        const auto count =
            std::min(destination.size() - offset, static_cast<size_t>(std::numeric_limits<ssize_t>::max()));
        const auto bytes_read = ::read(fd, destination.data() + offset, count);
        const auto error = errno;
        if (bytes_read == -1 && error == EINTR) {
            continue;
        }
        check(bytes_read >= 0, "FileBuffer: failed to read file: {}: {}", path,
              [error] { return std::error_code{error, std::generic_category()}.message(); });
        check(bytes_read != 0, "FileBuffer: unexpected EOF (file may have been truncated): {}: read {} of {} bytes",
              path, offset, destination.size());
        assert_check([=] { return static_cast<size_t>(bytes_read) <= count; },
                     "FileBuffer: read exceeded the requested byte count");
        offset += static_cast<size_t>(bytes_read);
    }
}

} // namespace detail

// FileBuffer exposes file contents from owned storage for read-only access.
// It provides an RAII wrapper around a complete read(2) of a regular file.
//
// For dictionary files that are read sequentially once (e.g. jieba.dict.utf8 at ~5MB / 350K lines),
// reading into one buffer avoids the per-line allocation and copying of ifstream/getline.
// The owned buffer is accessed via a single std::string_view,
// and downstream consumers (lines_view, split_view) produce further string_views that point directly
// into that buffer — avoiding further copies during parsing.
//
// Reads proceed sequentially, retrying interruptions and continuing after short reads.
// After loading, the buffer remains valid independently of the file on disk.
//
// Requires a regular file; I/O failures and detected changes during reading throw exceptions.
// Modification checks are best-effort and do not provide an atomic snapshot.
// Publish updates by atomic replacement to avoid mixing file versions during reading.
//
// Usage:
//   auto file = FileBuffer("dict/jieba.dict.utf8");
//   auto content = file.content();  // std::string_view over the entire file
//
// FileBuffer owns a read buffer so external truncation cannot invalidate its views.
// Detected file changes and I/O failures throw; metadata checks do not guarantee an atomic snapshot.
class FileBuffer {
public:
    // Delegation completes the empty object first, so construction failures run its destructor.
    explicit FileBuffer(std::string_view path) : FileBuffer() {
        check(path.find('\0') == std::string_view::npos, "FileBuffer: file path contains an embedded null byte");

        // open(2) requires a null-terminated path
        const auto null_terminated_path = std::string{path};
        // Avoid blocking on a FIFO before fstat can reject non-regular files.
        fd_ = ::open(null_terminated_path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);

        check(fd_ != -1, "FileBuffer: failed to open file: {}: {}", path,
              [error = errno] { return std::error_code{error, std::generic_category()}.message(); });

        struct ::stat st{};
        const auto stat_result = ::fstat(fd_, &st);
        check(stat_result == 0, "FileBuffer: failed to stat file: {}: {}", path,
              [error = errno] { return std::error_code{error, std::generic_category()}.message(); });
        check(S_ISREG(st.st_mode), "FileBuffer: not a regular file: {}", path);
        check(std::in_range<size_t>(st.st_size) && std::in_range<std::ptrdiff_t>(st.st_size),
              "FileBuffer: file size is out of range: {}", path);
        size_ = static_cast<size_t>(st.st_size);

        // Even an empty, successfully loaded file owns storage, distinguishing it from a moved-from object.
        data_.reset(new (std::nothrow) char[std::max(size_, size_t{1})]);
        check(data_ != nullptr, "FileBuffer: failed to allocate {} bytes for file: {}", size_, path);
        detail::read_file_contents(fd_, std::span<char>{data_.get(), size_}, path);

        // Check file size and modification timestamps against the metadata taken before reading.
        // These checks detect changes without retaining any reference to the file-backed pages.
        struct ::stat after{};
        const auto final_stat_result = ::fstat(fd_, &after);
        check(final_stat_result == 0, "FileBuffer: failed to stat file after reading: {}: {}", path,
              [error = errno] { return std::error_code{error, std::generic_category()}.message(); });
        const auto unchanged = st.st_size == after.st_size && st.st_mtim.tv_sec == after.st_mtim.tv_sec
                               && st.st_mtim.tv_nsec == after.st_mtim.tv_nsec
                               && st.st_ctim.tv_sec == after.st_ctim.tv_sec
                               && st.st_ctim.tv_nsec == after.st_ctim.tv_nsec;
        check(unchanged, "FileBuffer: file changed while reading: {}", path);

        // On Linux close releases the descriptor even on EINTR; never retry a possibly reused descriptor.
        const auto close_result = ::close(std::exchange(fd_, -1));
        check(close_result == 0, "FileBuffer: failed to close file: {}: {}", path,
              [error = errno] { return std::error_code{error, std::generic_category()}.message(); });
    }

    ~FileBuffer() noexcept {
        // Successful loads close explicitly; exception cleanup must not throw a second exception.
        if (fd_ != -1) {
            ::close(fd_);
        }
    }

    // Move-only: transferring ownership of the file buffer.
    // The owned storage keeps its address when moved.
    FileBuffer(FileBuffer &&other) noexcept
        : data_(std::move(other.data_)), size_(std::exchange(other.size_, 0)), fd_(std::exchange(other.fd_, -1)) {
    }

    auto operator=(FileBuffer &&other) noexcept -> FileBuffer & {
        if (this != &other) {
            if (fd_ != -1) {
                ::close(fd_);
            }
            data_ = std::move(other.data_);
            size_ = std::exchange(other.size_, 0);
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    FileBuffer(const FileBuffer &) = delete;
    auto operator=(const FileBuffer &) -> FileBuffer & = delete;

    // content returns a string_view spanning the entire file buffer.
    // The view remains valid while this object or its move destination owns the buffer.
    // Move assignment invalidates views into the destination's previous buffer.
    [[nodiscard]] auto content() const noexcept -> std::string_view {
        return size_ == 0 ? std::string_view{} : std::string_view{data_.get(), size_};
    }

    // size returns the file size in bytes.
    [[nodiscard]] auto size() const noexcept -> size_t {
        return size_;
    }

    // is_open reports whether this object owns successfully loaded file content.
    // An empty file is valid, while a moved-from object is not; the descriptor is already closed.
    [[nodiscard]] auto is_open() const noexcept -> bool {
        return data_ != nullptr;
    }

private:
    FileBuffer() = default;

    std::unique_ptr<char[]> data_;
    size_t size_{0};
    int fd_{-1};
};

// read_file reads the given file path into a FileBuffer.
// File failures are reported through check before returning any content.
inline auto read_file(std::string_view path) -> FileBuffer {
    return FileBuffer{path};
}

} // namespace neo_cppjieba
