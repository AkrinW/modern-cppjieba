#pragma once

#include "neo/util/Logging.hpp"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include <sys/stat.h>

#ifdef _WIN32
// Keep the Windows SDK from defining min/max macros in public header consumers.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>

#include <sys/types.h>
#endif

namespace neo_cppjieba::detail::platform {

#ifdef _WIN32
// Public file paths are UTF-8; the Windows filesystem API accepts UTF-16.
inline auto windows_file_path(std::string_view path) -> std::wstring {
    if (path.empty()) {
        return {};
    }
    check(std::in_range<int>(path.size()), "FileBuffer: file path is too long");
    const auto length =
        ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()), nullptr, 0);
    check(length != 0, "FileBuffer: invalid UTF-8 file path: {}", path);
    auto result = std::wstring(static_cast<size_t>(length), L'\0');
    const auto converted = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(),
                                                 static_cast<int>(path.size()), result.data(), length);
    check(converted == length, "FileBuffer: failed to convert file path: {}", path);
    return result;
}

// Preserve subsecond write and metadata-change timestamps, which _stat64 does not expose.
inline auto file_timestamps(int fd, std::string_view path) -> FILE_BASIC_INFO {
    auto info = FILE_BASIC_INFO{};
    const auto handle = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
    const auto result = ::GetFileInformationByHandleEx(handle, FileBasicInfo, &info, sizeof(info));
    check(result != 0, "FileBuffer: failed to read file timestamps: {}: {}", path, [error = ::GetLastError()] {
        return std::error_code{static_cast<int>(error), std::system_category()}.message();
    });
    return info;
}
#endif

// Close the owning CRT/POSIX descriptor without retrying a potentially reused descriptor.
inline auto close_file_descriptor(int fd) noexcept -> int {
#ifdef _WIN32
    return ::_close(fd);
#else
    return ::close(fd);
#endif
}

// Fill owned storage, retrying interrupted reads and reporting I/O errors or premature EOF through check.
inline auto read_file_contents(int fd, std::span<char> destination, std::string_view path) -> void {
    assert_check([fd] { return fd >= 0; }, "FileBuffer: invalid internal file descriptor");

    auto offset = size_t{0};
    while (offset < destination.size()) {
#ifdef _WIN32
        const auto count =
            std::min(destination.size() - offset, static_cast<size_t>(std::numeric_limits<DWORD>::max()));
        auto bytes_read = DWORD{0};
        // ReadFile reports access errors without invoking the CRT invalid-parameter handler.
        const auto result = ::ReadFile(reinterpret_cast<HANDLE>(::_get_osfhandle(fd)), destination.data() + offset,
                                       static_cast<DWORD>(count), &bytes_read, nullptr);
        check(result != 0, "FileBuffer: failed to read file: {}: {}", path, [error = ::GetLastError()] {
            return std::error_code{static_cast<int>(error), std::system_category()}.message();
        });
#else
        const auto count =
            std::min(destination.size() - offset, static_cast<size_t>(std::numeric_limits<ssize_t>::max()));
        const auto bytes_read = ::read(fd, destination.data() + offset, count);
        const auto error = errno;
        if (bytes_read == -1 && error == EINTR) {
            continue;
        }
        check(bytes_read >= 0, "FileBuffer: failed to read file: {}: {}", path,
              [error] { return std::error_code{error, std::generic_category()}.message(); });
#endif
        check(bytes_read != 0, "FileBuffer: unexpected EOF (file may have been truncated): {}: read {} of {} bytes",
              path, offset, destination.size());
        assert_check([=] { return static_cast<size_t>(bytes_read) <= count; },
                     "FileBuffer: read exceeded the requested byte count");
        offset += static_cast<size_t>(bytes_read);
    }
}

// Open an independent binary input descriptor; ownership transfers to the caller.
inline auto open_file(std::string_view path) -> int {
    check(path.find('\0') == std::string_view::npos, "FileBuffer: file path contains an embedded null byte");

    // open(2) requires a null-terminated path
#ifdef _WIN32
    const auto null_terminated_path = windows_file_path(path);
    const auto fd = ::_wopen(null_terminated_path.c_str(), _O_RDONLY | _O_BINARY | _O_NOINHERIT);
#else
    const auto null_terminated_path = std::string{path};
    // Avoid blocking on a FIFO before fstat can reject non-regular files.
    const auto fd = ::open(null_terminated_path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
#endif

    check(fd != -1, "FileBuffer: failed to open file: {}: {}", path,
          [error = errno] { return std::error_code{error, std::generic_category()}.message(); });
    return fd;
}

// Store timestamps at their original precision without leaking native stat structures.
struct FileTimestamp {
    std::int64_t seconds;
    std::int64_t nanoseconds;

    auto operator==(const FileTimestamp &) const noexcept -> bool = default;
};

// The file-loading layer compares snapshots of size and modification metadata.
struct FileMetadata {
    std::int64_t size;
    bool regular;
    FileTimestamp modified;
    FileTimestamp changed;

    auto operator==(const FileMetadata &) const noexcept -> bool = default;
};

// Identify which snapshot failed while retaining the existing I/O diagnostic.
enum class FileReadPhase { BeforeRead, AfterRead };

// Capture metadata from an open descriptor; external I/O errors follow the configured check path.
inline auto file_metadata(int fd, std::string_view path, FileReadPhase phase) -> FileMetadata {
    assert_check([fd] { return fd >= 0; }, "FileBuffer: invalid internal file descriptor");
#ifdef _WIN32
    struct ::_stat64 st{};
    const auto stat_result = ::_fstat64(fd, &st);
#else
    struct ::stat st{};
    const auto stat_result = ::fstat(fd, &st);
#endif
    check(stat_result == 0, "FileBuffer: failed to stat file{}: {}: {}",
          phase == FileReadPhase::AfterRead ? " after reading" : "", path,
          [error = errno] { return std::error_code{error, std::generic_category()}.message(); });
#ifdef _WIN32
    const auto regular = (st.st_mode & _S_IFMT) == _S_IFREG;
    if (!regular) {
        return {st.st_size, false, {}, {}};
    }
    const auto timestamps = file_timestamps(fd, path);
    constexpr auto ticks_per_second = std::int64_t{10000000};
    constexpr auto nanoseconds_per_tick = std::int64_t{100};
    return {st.st_size,
            true,
            {timestamps.LastWriteTime.QuadPart / ticks_per_second,
             (timestamps.LastWriteTime.QuadPart % ticks_per_second) * nanoseconds_per_tick},
            {timestamps.ChangeTime.QuadPart / ticks_per_second,
             (timestamps.ChangeTime.QuadPart % ticks_per_second) * nanoseconds_per_tick}};
#elif defined(__APPLE__)
    return {st.st_size,
            S_ISREG(st.st_mode),
            {st.st_mtimespec.tv_sec, st.st_mtimespec.tv_nsec},
            {st.st_ctimespec.tv_sec, st.st_ctimespec.tv_nsec}};
#else
    return {st.st_size,
            S_ISREG(st.st_mode),
            {st.st_mtim.tv_sec, st.st_mtim.tv_nsec},
            {st.st_ctim.tv_sec, st.st_ctim.tv_nsec}};
#endif
}

/// A file path split for replacement: the prefix up to and including the last separator, the file name after it,
/// and the directory whose entry the replacement changes.
struct FileLocation {
    std::string prefix;
    std::string name;
    std::string directory;
};

// A path without a separator names a file in the current directory.
inline auto split_file_path(std::string_view path) -> FileLocation {
#ifdef _WIN32
    constexpr auto separators = std::string_view{"/\\"};
#else
    constexpr auto separators = std::string_view{"/"};
#endif
    const auto split = path.find_last_of(separators);
    if (split == std::string_view::npos) {
        return {{}, std::string{path}, "."};
    }
    const auto prefix = path.substr(0, split + 1);
    return {std::string{prefix}, std::string{path.substr(split + 1)}, std::string{prefix}};
}

// A null-terminated path in the file system API's encoding, converted once so cleanup needs no allocation.
#ifdef _WIN32
using NativePath = std::wstring;
#else
using NativePath = std::string;
#endif

// Convert a UTF-8 path that has no embedded null byte.
inline auto native_path(std::string_view path) -> NativePath {
    assert_check([&] { return path.find('\0') == std::string_view::npos; }, "FileReplace: unchecked file path");
#ifdef _WIN32
    return windows_file_path(path);
#else
    return std::string{path};
#endif
}

// Create a file that must not exist yet, for writing; returns -1 with errno set on failure, EEXIST when the name
// is taken. The file gets the default permissions of any newly created file.
inline auto try_create_new_file(const NativePath &path) -> int {
#ifdef _WIN32
    return ::_wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT, _S_IREAD | _S_IWRITE);
#else
    return ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
#endif
}

// Write every byte, retrying interrupted and short writes; I/O errors are reported through check.
inline auto write_file_contents(int fd, std::span<const std::byte> source, std::string_view path) -> void {
    assert_check([fd] { return fd >= 0; }, "FileReplace: invalid internal file descriptor");

    auto offset = size_t{0};
    while (offset < source.size()) {
#ifdef _WIN32
        const auto count = std::min(source.size() - offset, static_cast<size_t>(std::numeric_limits<DWORD>::max()));
        auto bytes_written = DWORD{0};
        const auto result = ::WriteFile(reinterpret_cast<HANDLE>(::_get_osfhandle(fd)), source.data() + offset,
                                        static_cast<DWORD>(count), &bytes_written, nullptr);
        check(result != 0, "FileReplace: failed to write file: {}: {}", path, [error = ::GetLastError()] {
            return std::error_code{static_cast<int>(error), std::system_category()}.message();
        });
#else
        const auto count = std::min(source.size() - offset, static_cast<size_t>(std::numeric_limits<ssize_t>::max()));
        const auto bytes_written = ::write(fd, source.data() + offset, count);
        const auto error = errno;
        if (bytes_written == -1 && error == EINTR) {
            continue;
        }
        check(bytes_written >= 0, "FileReplace: failed to write file: {}: {}", path,
              [error] { return std::error_code{error, std::generic_category()}.message(); });
#endif
        check(bytes_written != 0, "FileReplace: writing made no progress: {}", path);
        offset += static_cast<size_t>(bytes_written);
    }
}

// Flush written data to storage before the file becomes visible under its final name.
inline auto sync_file(int fd, std::string_view path) -> void {
    assert_check([fd] { return fd >= 0; }, "FileReplace: invalid internal file descriptor");
#ifdef _WIN32
    const auto result = ::FlushFileBuffers(reinterpret_cast<HANDLE>(::_get_osfhandle(fd)));
    check(result != 0, "FileReplace: failed to flush file: {}: {}", path, [error = ::GetLastError()] {
        return std::error_code{static_cast<int>(error), std::system_category()}.message();
    });
#else
#ifdef __APPLE__
    // Apple's fsync leaves data in the drive cache; F_FULLFSYNC flushes it where the file system supports it.
    if (::fcntl(fd, F_FULLFSYNC) == 0) {
        return;
    }
#endif
    auto result = ::fsync(fd);
    while (result == -1 && errno == EINTR) {
        result = ::fsync(fd);
    }
    check(result == 0, "FileReplace: failed to flush file: {}: {}", path,
          [error = errno] { return std::error_code{error, std::generic_category()}.message(); });
#endif
}

// Atomically move a file over a destination in the same directory, replacing any existing file there.
// The UTF-8 path only labels diagnostics.
inline auto rename_over(const NativePath &from, const NativePath &to, std::string_view path) -> void {
#ifdef _WIN32
    const auto result = ::MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    check(result != 0, "FileReplace: failed to replace file: {}: {}", path, [error = ::GetLastError()] {
        return std::error_code{static_cast<int>(error), std::system_category()}.message();
    });
#else
    check(std::rename(from.c_str(), to.c_str()) == 0, "FileReplace: failed to replace file: {}: {}", path,
          [error = errno] { return std::error_code{error, std::generic_category()}.message(); });
#endif
}

// Persist the directory entry a rename changed; MOVEFILE_WRITE_THROUGH already did so on Windows.
// File systems that cannot sync directories report EINVAL, leaving the rename as durable as they make it.
inline auto sync_directory(std::string_view directory, std::string_view path) -> void {
#ifdef _WIN32
    static_cast<void>(directory);
    static_cast<void>(path);
#else
    const auto null_terminated_directory = std::string{directory};
    const auto fd = ::open(null_terminated_directory.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY);
    check(fd != -1, "FileReplace: replaced {} but could not open its directory to sync it: {}", path,
          [error = errno] { return std::error_code{error, std::generic_category()}.message(); });
    const auto result = ::fsync(fd);
    const auto error = errno;
    close_file_descriptor(fd);
    check(result == 0 || error == EINVAL, "FileReplace: replaced {} but failed to sync its directory: {}", path,
          [error] { return std::error_code{error, std::generic_category()}.message(); });
#endif
}

// Remove a file during cleanup; failures are ignored because the original error is already being reported.
inline auto remove_file(const NativePath &path) noexcept -> void {
#ifdef _WIN32
    static_cast<void>(::_wunlink(path.c_str()));
#else
    static_cast<void>(::unlink(path.c_str()));
#endif
}

} // namespace neo_cppjieba::detail::platform
