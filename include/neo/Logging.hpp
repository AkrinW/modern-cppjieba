#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <format>
#include <type_traits>
#include <source_location>
#include <string_view>
#include <thread>
#include <unistd.h>
#include <utility>

namespace neo_cppjieba {
// LogLevel represents the severity level of a log message, ranging from debug to fatal.
enum class LogLevel : uint8_t { LL_DEBUG, LL_INFO, LL_WARNING, LL_ERROR, LL_FATAL };

namespace detail {
// LogLevel enum to string mapping for log output.
inline constexpr auto LOG_LEVEL_ARRAY = std::array<std::string_view, 5>{"DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
constexpr auto log_level_name(LogLevel level) -> std::string_view {
    return LOG_LEVEL_ARRAY[static_cast<size_t>(level)];
}

// kCompileTimeMinLevel is a compile-time constant that indicates the minimum log level to be compiled into the binary.
#ifndef NDEBUG
inline constexpr auto kCompileTimeMinLevel = LogLevel::LL_DEBUG;
inline constexpr auto kNoDebug = false;
#else
inline constexpr auto kCompileTimeMinLevel = LogLevel::LL_FATAL;
inline constexpr auto kNoDebug = true;
#endif

// LOG_TIME_FORMAT is the format string used to format the timestamp in log messages.
inline constexpr auto LOG_TIME_FORMAT = "%Y-%m-%d %H:%M:%S";

// LOG_MAX_TIME_BUFFER_SIZE is the maximum size of the buffer used to store the formatted timestamp in log messages.
inline constexpr auto LOG_MAX_TIME_BUFFER_SIZE = size_t{32};

// LOG_MAX_BUFFER_SIZE is the maximum size of the buffer used to store the formatted log message before writing to
// stderr.
inline constexpr auto LOG_MAX_BUFFER_SIZE = size_t{512};

// cur_time fills the provided buffer with the current time formatted according to LOG_TIME_FORMAT.
inline auto cur_time() -> std::array<char, LOG_MAX_TIME_BUFFER_SIZE> {
    auto timeNow = std::time(nullptr);
    auto tmNow = std::tm{};
#if defined(_WIN32) || defined(_WIN64)
    auto e = localtime_s(&tmNow, &timeNow);
    assert(e == 0);
#else
    auto *tm_tmp = localtime_r(&timeNow, &tmNow);
    assert(tm_tmp != nullptr);
    (void)tm_tmp;
#endif
    auto buf = std::array<char, LOG_MAX_TIME_BUFFER_SIZE>{};
    std::strftime(buf.data(), buf.size(), LOG_TIME_FORMAT, &tmNow);
    return buf;
}

// log_impl is the internal function that performs the actual logging. It formats the log message and writes it to
// stderr.
template <LogLevel Level, typename... Args>
inline auto log_impl(std::format_string<Args...> fmt, const std::source_location &loc, Args &&...args) -> void {
    if constexpr (Level < kCompileTimeMinLevel) {
        return;
    }

    auto buffer = std::array<char, LOG_MAX_BUFFER_SIZE>{};
    auto time_buf = cur_time();
    auto tid_hash = std::hash<std::thread::id>{}(std::this_thread::get_id());

    auto it =
        std::format_to_n(buffer.begin(), LOG_MAX_BUFFER_SIZE, "{}[pid:{} tid:{:04x}]<{}> {}:{} ", time_buf.data(),
                         getpid(), static_cast<uint16_t>(tid_hash), log_level_name(Level), loc.file_name(), loc.line());
    auto used = static_cast<size_t>(it.out - buffer.data());

    auto result = std::format_to_n(buffer.begin() + used, LOG_MAX_BUFFER_SIZE - used, fmt, std::forward<Args>(args)...);
    auto total = static_cast<size_t>(result.out - buffer.data());

    if (total < LOG_MAX_BUFFER_SIZE) {
        buffer[total] = '\n';
        total += 1;
    } else {
        buffer[LOG_MAX_BUFFER_SIZE - 1] = '\n';
        total = LOG_MAX_BUFFER_SIZE;
    }

    std::fwrite(buffer.data(), sizeof(char), total, stderr);
    if constexpr (Level == LogLevel::LL_FATAL) {
        std::fflush(stderr);
        // throw exception
        throw std::runtime_error{std::string{buffer.data(), total}};
    }
}

// LogFormatString is a helper struct that holds a format string and its source location for logging purposes. It allows
// for compile-time format string checking and provides context for log messages.
template <typename... Args>
struct LogFormatString {
    std::format_string<Args...> fmt;
    std::source_location loc;

    template <typename StringType>
    consteval LogFormatString(StringType &&s, const std::source_location &l = std::source_location::current())
        : fmt(std::forward<StringType>(s)), loc(l) {
    }
};
} // namespace detail

// log is a helper function that formats a log message with the given format string and arguments, and logs it at the
// specified log level.
template <LogLevel Level, typename... Args>
inline auto log(std::type_identity_t<detail::LogFormatString<Args...>> fmt, Args &&...args) -> void {
    detail::log_impl<Level>(fmt.fmt, fmt.loc, std::forward<Args>(args)...);
}

template <LogLevel Level>
inline auto log(const std::source_location &loc = std::source_location::current()) -> void {
    detail::log_impl<Level>("", loc);
}

// check is a helper function that evaluates an expression and logs a fatal error if the expression is false. It uses
// the same formatting mechanism as log_impl to provide detailed error messages.
template <typename... Args>
inline auto check(bool expr, std::type_identity_t<detail::LogFormatString<Args...>> fmt, Args &&...args) -> void {
    if (!expr) [[unlikely]] {
        detail::log_impl<LogLevel::LL_FATAL>(fmt.fmt, fmt.loc, std::forward<Args>(args)...);
    }
}

inline auto check(bool expr, const std::source_location &loc = std::source_location::current()) -> void {
    if (!expr) [[unlikely]] {
        detail::log_impl<LogLevel::LL_FATAL>("", loc);
    }
}

// assert_check evaluates a predicate lambda only in debug builds (NDEBUG not defined).
// In release builds the lambda is never invoked, avoiding side-effect evaluation.
template <typename F, typename... Args>
    requires std::is_invocable_r_v<bool, F>
inline auto assert_check(F &&predicate, std::type_identity_t<detail::LogFormatString<Args...>> fmt, Args &&...args)
    -> void {
    if constexpr (detail::kNoDebug) {
        return;
    } else {
        if (!std::forward<F>(predicate)()) [[unlikely]] {
            detail::log_impl<LogLevel::LL_FATAL>(fmt.fmt, fmt.loc, std::forward<Args>(args)...);
        }
    }
}

template <typename F>
    requires std::is_invocable_r_v<bool, F>
inline auto assert_check(F &&predicate, const std::source_location &loc = std::source_location::current()) -> void {
    if constexpr (detail::kNoDebug) {
        return;
    } else {
        if (!std::forward<F>(predicate)()) [[unlikely]] {
            detail::log_impl<LogLevel::LL_FATAL>("", loc);
        }
    }
}

} // namespace neo_cppjieba
