#pragma once

#include "Config.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <exception>
#include <format>
#include <functional>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
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

// ANSI color escape sequences per log level.
inline constexpr auto LOG_LEVEL_COLOR_ARRAY = std::array<std::string_view, 5>{
    "\033[36m",   // DEBUG: cyan
    "\033[32m",   // INFO: green
    "\033[33m",   // WARNING: yellow
    "\033[31m",   // ERROR: red
    "\033[1;31m", // FATAL: bold red
};
inline constexpr auto LOG_COLOR_RESET = std::string_view{"\033[0m"};

constexpr auto log_level_color(LogLevel level) -> std::string_view {
    return LOG_LEVEL_COLOR_ARRAY[static_cast<size_t>(level)];
}

inline auto stderr_is_tty() -> bool {
    static const auto result = (isatty(STDERR_FILENO) != 0);
    return result;
}

// kCompileTimeMinLevel is a compile-time constant that indicates the minimum log level to be compiled into the binary.
inline constexpr auto kCompileTimeMinLevel = compile_config::is_debug_build ? LogLevel::LL_DEBUG : LogLevel::LL_INFO;
inline constexpr auto kNoDebug = !compile_config::is_debug_build;

template <typename Condition>
concept CheckPredicate = std::invocable<Condition> && std::convertible_to<std::invoke_result_t<Condition>, bool>;

template <typename Condition>
concept CheckCondition = std::convertible_to<Condition, bool> || CheckPredicate<Condition>;

// Treat zero-argument callables as lazy values while preserving ordinary string arguments.
template <typename Value>
concept LogArgInvocable = std::invocable<Value> && !std::is_convertible_v<Value, std::string_view>
                          && !std::is_convertible_v<Value, const char *>;

// Invoke diagnostic producers only after a log level or failed check has selected the logging path.
template <typename Value>
[[nodiscard]] auto eval_log_arg(Value &&arg) -> decltype(auto) {
    if constexpr (LogArgInvocable<Value>) {
        return std::invoke(std::forward<Value>(arg));
    } else {
        return std::forward<Value>(arg);
    }
}

template <typename Value>
using LogRawEvalType = decltype(eval_log_arg(std::declval<Value>()));

// Match std::format's deduction: retain lvalue references and normalize rvalue references to values.
template <typename Value>
using LogEvalType = std::conditional_t<std::is_lvalue_reference_v<LogRawEvalType<Value>>, LogRawEvalType<Value>,
                                       std::remove_reference_t<LogRawEvalType<Value>>>;

// Evaluate a bool-like value or predicate exactly once for a check.
template <CheckCondition Condition>
[[nodiscard]] constexpr auto eval_check_condition(Condition &&condition) -> bool {
    if constexpr (CheckPredicate<Condition>) {
        return static_cast<bool>(std::invoke(std::forward<Condition>(condition)));
    } else {
        return static_cast<bool>(std::forward<Condition>(condition));
    }
}

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

// Colored prefix format:
//   dim timestamp, dim [pid/tid], level-colored <LEVEL>, magenta file:line
inline constexpr auto LOG_COLOR_PREFIX_FMT =
    "\033[90m{}\033[0m[\033[90mpid:{} tid:{:04x}\033[0m]{}<{}>\033[0m\033[35m{}:{}\033[0m ";
inline constexpr auto LOG_PLAIN_PREFIX_FMT = "{}[pid:{} tid:{:04x}]<{}>{}:{} ";
inline constexpr auto LOG_COLOR_PRIVATE_PREFIX_FMT =
    "\033[90m{}\033[0m[\033[90mpid:{} tid:{:04x}\033[0m]{}<{}>\033[0m ";
inline constexpr auto LOG_PLAIN_PRIVATE_PREFIX_FMT = "{}[pid:{} tid:{:04x}]<{}> ";

// log_impl is the internal function that performs the actual logging. It formats the log message and writes it to
// stderr.
template <LogLevel Level, LogConfiguration Config, typename... Args>
inline auto log_impl(std::format_string<LogEvalType<Args>...> fmt, const std::source_location &loc,
                     Args &&...args) noexcept(Level == LogLevel::LL_FATAL) -> void {
    if constexpr (Level < kCompileTimeMinLevel) {
        return;
    }

    constexpr auto color_prefix_fmt =
        Config::show_source_location ? LOG_COLOR_PREFIX_FMT : LOG_COLOR_PRIVATE_PREFIX_FMT;
    constexpr auto plain_prefix_fmt =
        Config::show_source_location ? LOG_PLAIN_PREFIX_FMT : LOG_PLAIN_PRIVATE_PREFIX_FMT;
    auto buffer = std::array<char, LOG_MAX_BUFFER_SIZE>{};
    const auto time_buf = cur_time();
    const auto tid_hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
    const auto use_color = stderr_is_tty();
    const auto pid = getpid();
    const auto tid_short = static_cast<uint16_t>(tid_hash);

    size_t prefix_len = 0;
    if (use_color) {
        const auto it =
            std::format_to_n(buffer.begin(), LOG_MAX_BUFFER_SIZE, color_prefix_fmt, time_buf.data(), pid, tid_short,
                             log_level_color(Level), log_level_name(Level), loc.file_name(), loc.line());
        prefix_len = static_cast<size_t>(it.out - buffer.data());
    } else {
        const auto it = std::format_to_n(buffer.begin(), LOG_MAX_BUFFER_SIZE, plain_prefix_fmt, time_buf.data(), pid,
                                         tid_short, log_level_name(Level), loc.file_name(), loc.line());
        prefix_len = static_cast<size_t>(it.out - buffer.data());
    }

    const auto result = std::format_to_n(buffer.begin() + prefix_len, LOG_MAX_BUFFER_SIZE - prefix_len, fmt,
                                         eval_log_arg(std::forward<Args>(args))...);
    auto total = static_cast<size_t>(result.out - buffer.data());

    if (total < LOG_MAX_BUFFER_SIZE) {
        buffer[total] = '\n';
        total += 1;
    } else {
        buffer[LOG_MAX_BUFFER_SIZE - 1] = '\n';
        total = LOG_MAX_BUFFER_SIZE;
    }

    std::fwrite(buffer.data(), sizeof(char), total, stderr);
    if constexpr (Level == LogLevel::LL_ERROR) {
        std::fflush(stderr);
        if (use_color) {
            // Build a plain version for the exception message (no ANSI codes).
            auto plain = std::array<char, LOG_MAX_BUFFER_SIZE>{};
            const auto pit = std::format_to_n(plain.begin(), LOG_MAX_BUFFER_SIZE, plain_prefix_fmt, time_buf.data(),
                                              pid, tid_short, log_level_name(Level), loc.file_name(), loc.line());
            const auto plain_prefix = static_cast<size_t>(pit.out - plain.data());
            const auto msg_len = total - prefix_len;
            const auto copy_len = std::min(msg_len, LOG_MAX_BUFFER_SIZE - plain_prefix);
            std::copy_n(buffer.data() + prefix_len, copy_len, plain.data() + plain_prefix);
            throw typename Config::Exception{std::string{plain.data(), plain_prefix + copy_len}};
        }
        throw typename Config::Exception{std::string{buffer.data(), total}};
    } else if constexpr (Level == LogLevel::LL_FATAL) {
        std::fflush(stderr);
        std::terminate();
    }
    // Fatal diagnostics must terminate even if a lazy argument or formatter throws.
    // The conditional noexcept specification enforces this on diagnostic failure.
}

// LogFormatString is a helper struct that holds a format string and its source location for logging purposes. It allows
// for compile-time format string checking and provides context for log messages.
template <LogConfiguration Config, typename... Args>
struct LogFormatString {
    std::format_string<Args...> fmt;
    std::source_location loc;

    template <typename StringType>
    consteval LogFormatString(StringType &&s, const std::source_location &l = std::source_location::current())
        : fmt(std::forward<StringType>(s)), loc(Config::show_source_location ? l : std::source_location{}) {
    }
};
} // namespace detail

// log is a helper function that formats a log message with the given format string and arguments, and logs it at the
// specified log level.
// Pass expensive arguments as zero-argument callables so filtered log levels skip their computation.
template <LogLevel Level, LogConfiguration Config = LogConfig, typename... Args>
inline auto log(std::type_identity_t<detail::LogFormatString<Config, detail::LogEvalType<Args>...>> fmt,
                Args &&...args) noexcept(Level == LogLevel::LL_FATAL) -> void {
    detail::log_impl<Level, Config>(fmt.fmt, fmt.loc, std::forward<Args>(args)...);
}

// Log an empty message with the same severity and configuration rules.
template <LogLevel Level, LogConfiguration Config = LogConfig>
inline auto log(const std::source_location &loc = std::source_location::current()) noexcept(Level == LogLevel::LL_FATAL)
    -> void {
    detail::log_impl<Level, Config>("", Config::show_source_location ? loc : std::source_location{});
}

// check is a helper function that evaluates an expression and logs a fatal error if the expression is false. It uses
// the same formatting mechanism as log_impl to provide detailed error messages.
// Failed checks now use ERROR to throw Config::Exception; FATAL terminates.
template <LogConfiguration Config = LogConfig, detail::CheckCondition Condition, typename... Args>
inline auto check(Condition &&condition,
                  std::type_identity_t<detail::LogFormatString<Config, detail::LogEvalType<Args>...>> fmt,
                  Args &&...args) -> void {
    if (!detail::eval_check_condition(std::forward<Condition>(condition))) [[unlikely]] {
        detail::log_impl<LogLevel::LL_ERROR, Config>(fmt.fmt, fmt.loc, std::forward<Args>(args)...);
    }
}

// Check a condition without an additional diagnostic message.
template <LogConfiguration Config = LogConfig, detail::CheckCondition Condition>
inline auto check(Condition &&condition, const std::source_location &loc = std::source_location::current()) -> void {
    if (!detail::eval_check_condition(std::forward<Condition>(condition))) [[unlikely]] {
        detail::log_impl<LogLevel::LL_ERROR, Config>("", Config::show_source_location ? loc : std::source_location{});
    }
}

// assert_check evaluates a predicate lambda only in debug builds (NDEBUG not defined).
// In release builds the lambda is never invoked, avoiding side-effect evaluation.
template <LogConfiguration Config = LogConfig, detail::CheckPredicate F, typename... Args>
inline auto assert_check(F &&predicate,
                         std::type_identity_t<detail::LogFormatString<Config, detail::LogEvalType<Args>...>> fmt,
                         Args &&...args) -> void {
    if constexpr (detail::kNoDebug) {
        return;
    } else {
        if (!detail::eval_check_condition(std::forward<F>(predicate))) [[unlikely]] {
            detail::log_impl<LogLevel::LL_ERROR, Config>(fmt.fmt, fmt.loc, std::forward<Args>(args)...);
        }
    }
}

// Preserve the debug-only throwing check when no diagnostic message is supplied.
template <LogConfiguration Config = LogConfig, detail::CheckPredicate F>
inline auto assert_check(F &&predicate, const std::source_location &loc = std::source_location::current()) -> void {
    if constexpr (detail::kNoDebug) {
        return;
    } else {
        if (!detail::eval_check_condition(std::forward<F>(predicate))) [[unlikely]] {
            detail::log_impl<LogLevel::LL_ERROR, Config>("",
                                                         Config::show_source_location ? loc : std::source_location{});
        }
    }
}

} // namespace neo_cppjieba
