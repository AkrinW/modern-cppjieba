#include "gtest/gtest.h"
#include "neo/Config.hpp"
#include "neo/detail/Logging.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <format>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>

using namespace neo_cppjieba;

// ─── LogLevel name mapping ──────────────────────────────────────────────────

TEST(LoggingTest, LogLevelNameDebug) {
    EXPECT_EQ(detail::log_level_name(LogLevel::LL_DEBUG), "DEBUG");
}

TEST(LoggingTest, LogLevelNameInfo) {
    EXPECT_EQ(detail::log_level_name(LogLevel::LL_INFO), "INFO");
}

TEST(LoggingTest, LogLevelNameWarning) {
    EXPECT_EQ(detail::log_level_name(LogLevel::LL_WARNING), "WARN");
}

TEST(LoggingTest, LogLevelNameError) {
    EXPECT_EQ(detail::log_level_name(LogLevel::LL_ERROR), "ERROR");
}

TEST(LoggingTest, LogLevelNameFatal) {
    EXPECT_EQ(detail::log_level_name(LogLevel::LL_FATAL), "FATAL");
}

// ─── LogLevel ordering ─────────────────────────────────────────────────────

TEST(LoggingTest, LogLevelOrdering) {
    EXPECT_LT(static_cast<uint8_t>(LogLevel::LL_DEBUG), static_cast<uint8_t>(LogLevel::LL_INFO));
    EXPECT_LT(static_cast<uint8_t>(LogLevel::LL_INFO), static_cast<uint8_t>(LogLevel::LL_WARNING));
    EXPECT_LT(static_cast<uint8_t>(LogLevel::LL_WARNING), static_cast<uint8_t>(LogLevel::LL_ERROR));
    EXPECT_LT(static_cast<uint8_t>(LogLevel::LL_ERROR), static_cast<uint8_t>(LogLevel::LL_FATAL));
}

// ─── LOG_LEVEL_ARRAY ────────────────────────────────────────────────────────

TEST(LoggingTest, LogLevelArraySize) {
    EXPECT_EQ(detail::LOG_LEVEL_ARRAY.size(), 5u);
}

TEST(LoggingTest, LogLevelArrayValues) {
    EXPECT_EQ(detail::LOG_LEVEL_ARRAY[0], "DEBUG");
    EXPECT_EQ(detail::LOG_LEVEL_ARRAY[1], "INFO");
    EXPECT_EQ(detail::LOG_LEVEL_ARRAY[2], "WARN");
    EXPECT_EQ(detail::LOG_LEVEL_ARRAY[3], "ERROR");
    EXPECT_EQ(detail::LOG_LEVEL_ARRAY[4], "FATAL");
}

// ─── LogLevel color mapping ─────────────────────────────────────────────────

TEST(LoggingTest, LogLevelColorArraySize) {
    EXPECT_EQ(detail::LOG_LEVEL_COLOR_ARRAY.size(), 5u);
}

TEST(LoggingTest, LogLevelColorDebugIsCyan) {
    EXPECT_EQ(detail::log_level_color(LogLevel::LL_DEBUG), "\033[36m");
}

TEST(LoggingTest, LogLevelColorInfoIsGreen) {
    EXPECT_EQ(detail::log_level_color(LogLevel::LL_INFO), "\033[32m");
}

TEST(LoggingTest, LogLevelColorWarningIsYellow) {
    EXPECT_EQ(detail::log_level_color(LogLevel::LL_WARNING), "\033[33m");
}

TEST(LoggingTest, LogLevelColorErrorIsRed) {
    EXPECT_EQ(detail::log_level_color(LogLevel::LL_ERROR), "\033[31m");
}

TEST(LoggingTest, LogLevelColorFatalIsBoldRed) {
    EXPECT_EQ(detail::log_level_color(LogLevel::LL_FATAL), "\033[1;31m");
}

TEST(LoggingTest, LogColorResetSequence) {
    EXPECT_EQ(detail::LOG_COLOR_RESET, "\033[0m");
}

// ─── FATAL exception message has no ANSI escape codes ────────────────────────
// ERROR now owns the throwing path; FATAL termination is tested below.

TEST(LoggingTest, ErrorExceptionMessageHasNoColorCodes) {
    try {
        log<LogLevel::LL_ERROR>("no color in exception");
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error &e) {
        auto what = std::string_view{e.what()};
        EXPECT_EQ(what.find("\033["), std::string_view::npos);
    }
}

// ─── Compile-time min level ─────────────────────────────────────────────────

TEST(LoggingTest, CompileTimeMinLevel) {
#ifndef NDEBUG
    EXPECT_EQ(detail::kCompileTimeMinLevel, LogLevel::LL_DEBUG);
#else
    EXPECT_EQ(detail::kCompileTimeMinLevel, LogLevel::LL_INFO);
#endif
}

// ─── cur_time returns non-empty formatted string ────────────────────────────

TEST(LoggingTest, CurTimeReturnsNonEmptyString) {
    auto buf = detail::cur_time();
    auto sv = std::string_view{buf.data()};
    EXPECT_FALSE(sv.empty());
    // Format: "YYYY-MM-DD HH:MM:SS" => at least 19 chars
    EXPECT_GE(sv.size(), 19u);
}

TEST(LoggingTest, CurTimeFormatPlausible) {
    auto buf = detail::cur_time();
    auto sv = std::string_view{buf.data()};
    // Should start with "20" (year 20xx)
    EXPECT_EQ(sv.substr(0, 2), "20");
    // Check separator positions: YYYY-MM-DD HH:MM:SS
    EXPECT_EQ(sv[4], '-');
    EXPECT_EQ(sv[7], '-');
    EXPECT_EQ(sv[10], ' ');
    EXPECT_EQ(sv[13], ':');
    EXPECT_EQ(sv[16], ':');
}

// ─── Constants ──────────────────────────────────────────────────────────────

TEST(LoggingTest, MaxTimeBufferSizeSufficient) {
    EXPECT_GE(detail::LOG_MAX_TIME_BUFFER_SIZE, 20u);
}

TEST(LoggingTest, MaxBufferSizeSufficient) {
    EXPECT_GE(detail::LOG_MAX_BUFFER_SIZE, 128u);
}

// ─── log() non-fatal levels write to stderr ─────────────────────────────────

namespace {

// An application-owned exception distinct from the default runtime error.
class ProjectError : public std::logic_error {
public:
    using std::logic_error::logic_error;
};

// Keep public diagnostics while selecting the application's exception type.
struct ProjectLogConfig {
    static constexpr auto show_source_location = true;
    static constexpr auto min_level = LogConfig::min_level;
    static constexpr auto buffer_size = LogConfig::buffer_size;
    using Exception = ProjectError;
};

// Suppress source locations independently of the build mode and exception type.
struct PrivateLogConfig {
    static constexpr auto show_source_location = false;
    static constexpr auto min_level = LogConfig::min_level;
    static constexpr auto buffer_size = LogConfig::buffer_size;
    using Exception = ProjectError;
};

// An incomplete configuration must be rejected at the logging API boundary.
struct MissingExceptionConfig {
    static constexpr auto show_source_location = true;
    static constexpr auto min_level = LogConfig::min_level;
    static constexpr auto buffer_size = LogConfig::buffer_size;
};

// An exception alias alone does not specify the source location policy.
struct MissingSourceLocationConfig {
    static constexpr auto min_level = LogConfig::min_level;
    static constexpr auto buffer_size = LogConfig::buffer_size;
    using Exception = ProjectError;
};

// A constructible message type still needs to be an exception.
struct InvalidExceptionConfig {
    static constexpr auto show_source_location = true;
    static constexpr auto min_level = LogConfig::min_level;
    static constexpr auto buffer_size = LogConfig::buffer_size;
    using Exception = std::string;
};

// An integer switch must not implicitly satisfy the boolean configuration contract.
struct IntegerSourceLocationConfig {
    static constexpr auto show_source_location = 1;
    static constexpr auto min_level = LogConfig::min_level;
    static constexpr auto buffer_size = LogConfig::buffer_size;
    using Exception = ProjectError;
};

// An uninitialized constant declaration cannot supply a compile-time policy value.
struct NonConstantSourceLocationConfig {
    static const bool show_source_location;
    static constexpr auto min_level = LogConfig::min_level;
    static constexpr auto buffer_size = LogConfig::buffer_size;
    using Exception = ProjectError;
};

// A high threshold filters ordinary messages without disabling failures.
struct HighThresholdLogConfig {
    static constexpr auto show_source_location = false;
    static constexpr auto min_level = LogLevel::LL_FATAL;
    static constexpr auto buffer_size = LogConfig::buffer_size;
    using Exception = ProjectError;
};

// A smaller record exercises bounded output with an explicitly enabled log level.
struct SmallBufferLogConfig {
    static constexpr auto show_source_location = false;
    static constexpr auto min_level = LogLevel::LL_DEBUG;
    static constexpr auto buffer_size = std::size_t{128};
    using Exception = ProjectError;
};

// A larger record preserves messages that exceed the default buffer capacity.
struct LargeBufferLogConfig {
    static constexpr auto show_source_location = false;
    static constexpr auto min_level = LogLevel::LL_DEBUG;
    static constexpr auto buffer_size = std::size_t{1024};
    using Exception = ProjectError;
};

// An out-of-range threshold cannot name a supported severity.
struct InvalidLogLevelConfig {
    static constexpr auto show_source_location = false;
    static constexpr auto min_level = static_cast<LogLevel>(255);
    static constexpr auto buffer_size = LogConfig::buffer_size;
    using Exception = ProjectError;
};

// A runtime threshold cannot control compile-time filtering.
struct NonConstantLogLevelConfig {
    static constexpr auto show_source_location = false;
    static const LogLevel min_level;
    static constexpr auto buffer_size = LogConfig::buffer_size;
    using Exception = ProjectError;
};

// A log record needs at least one byte for its terminating newline.
struct EmptyBufferLogConfig {
    static constexpr auto show_source_location = false;
    static constexpr auto min_level = LogConfig::min_level;
    static constexpr auto buffer_size = std::size_t{0};
    using Exception = ProjectError;
};

// A runtime capacity cannot size the stack-allocated log record.
struct NonConstantBufferLogConfig {
    static constexpr auto show_source_location = false;
    static constexpr auto min_level = LogConfig::min_level;
    static const std::size_t buffer_size;
    using Exception = ProjectError;
};

// Assertion death tests verify the terminate handler is invoked without leaving core dumps.
auto prepare_assertion_death_test() -> void {
    std::set_terminate([] { std::_Exit(73); });
}

// Helper: capture stderr output from a callable.
auto capture_stderr(auto &&fn) -> std::string {
    // Flush stderr first
    std::fflush(stderr);

    // Create a temporary file to redirect stderr
    auto *tmpf = std::tmpfile();
    EXPECT_NE(tmpf, nullptr);
    auto tmp_fd = fileno(tmpf);
    auto orig_fd = dup(STDERR_FILENO);

    dup2(tmp_fd, STDERR_FILENO);
    fn();
    std::fflush(stderr);
    dup2(orig_fd, STDERR_FILENO);
    close(orig_fd);

    // Read from the temp file
    std::fseek(tmpf, 0, SEEK_END);
    auto sz = std::ftell(tmpf);
    std::fseek(tmpf, 0, SEEK_SET);
    auto result = std::string(static_cast<size_t>(sz), '\0');
    std::fread(result.data(), 1, static_cast<size_t>(sz), tmpf);
    std::fclose(tmpf);
    return result;
}

} // namespace

TEST(LoggingTest, LogDebugWritesToStderr) {
    auto output = capture_stderr([] { log<LogLevel::LL_DEBUG>("hello {}", 42); });
    // In debug builds, this should produce output; in release, it may be empty.
#ifndef NDEBUG
    EXPECT_NE(output.find("DEBUG"), std::string::npos);
    EXPECT_NE(output.find("hello 42"), std::string::npos);
#else
    EXPECT_TRUE(output.empty());
#endif
}

TEST(LoggingTest, LogInfoWritesToStderr) {
    auto output = capture_stderr([] { log<LogLevel::LL_INFO>("info msg: {}", "test_value"); });
    EXPECT_NE(output.find("INFO"), std::string::npos);
    EXPECT_NE(output.find("info msg: test_value"), std::string::npos);
}

TEST(LoggingTest, LogWarningWritesToStderr) {
    auto output = capture_stderr([] { log<LogLevel::LL_WARNING>("warn: {}", 99); });
    EXPECT_NE(output.find("WARN"), std::string::npos);
    EXPECT_NE(output.find("warn: 99"), std::string::npos);
}

TEST(LoggingTest, LogErrorWritesToStderr) {
    const auto output =
        capture_stderr([] { EXPECT_THROW(log<LogLevel::LL_ERROR>("error code={}", 500), std::runtime_error); });
    EXPECT_NE(output.find("ERROR"), std::string::npos);
    EXPECT_NE(output.find("error code=500"), std::string::npos);
}

// ─── log() fatal level throws runtime_error ─────────────────────────────────
// FATAL now calls std::terminate, including when diagnostic evaluation fails.

TEST(LoggingTest, LogFatalCallsTerminate) {
    EXPECT_EXIT(
        {
            std::set_terminate([] { std::_Exit(73); });
            log<LogLevel::LL_FATAL>("fatal: {}", "crash");
        },
        ::testing::ExitedWithCode(73), "fatal: crash");
}

TEST(LoggingTest, LogFatalEvaluatesLazyMessageBeforeTerminating) {
    EXPECT_EXIT(
        {
            std::set_terminate([] { std::_Exit(73); });
            auto calls = 0;
            log<LogLevel::LL_FATAL>("fatal evaluations: {}", [&] { return ++calls; });
        },
        ::testing::ExitedWithCode(73), "fatal evaluations: 1");
}

// ─── log() output contains source location ──────────────────────────────────

TEST(LoggingTest, LogOutputContainsSourceFile) {
    auto output = capture_stderr([] { log<LogLevel::LL_INFO>("loc check"); });
    // Should contain the filename of this test file
    EXPECT_NE(output.find("logging_test.cpp"), std::string::npos);
}

// ─── log() output contains PID and TID ──────────────────────────────────────

TEST(LoggingTest, LogOutputContainsPid) {
    auto output = capture_stderr([] { log<LogLevel::LL_INFO>("pid check"); });
    auto pid_str = std::string{"pid:"} + std::to_string(getpid());
    EXPECT_NE(output.find(pid_str), std::string::npos);
}

TEST(LoggingTest, LogOutputContainsTid) {
    auto output = capture_stderr([] { log<LogLevel::LL_INFO>("tid check"); });
    EXPECT_NE(output.find("tid:"), std::string::npos);
}

// ─── log() output contains timestamp ────────────────────────────────────────

TEST(LoggingTest, LogOutputContainsTimestamp) {
    auto output = capture_stderr([] { log<LogLevel::LL_INFO>("time check"); });
    // Should contain year prefix "20"
    EXPECT_NE(output.find("20"), std::string::npos);
}

// ─── log() with no format args ──────────────────────────────────────────────

TEST(LoggingTest, LogWithNoArgs) {
    auto output = capture_stderr([] { log<LogLevel::LL_INFO>("plain message no args"); });
    EXPECT_NE(output.find("plain message no args"), std::string::npos);
}

// ─── log() with multiple args ───────────────────────────────────────────────

TEST(LoggingTest, LogWithMultipleArgs) {
    auto output = capture_stderr([] { log<LogLevel::LL_INFO>("a={} b={} c={}", 1, "two", 3.0); });
    EXPECT_NE(output.find("a=1 b=two c=3"), std::string::npos);
}

// ─── check() passes when expression is true ─────────────────────────────────

TEST(LoggingTest, CheckTrueDoesNotThrow) {
    EXPECT_NO_THROW(check(true, "should not fire"));
}

TEST(LoggingTest, CheckTrueWithArgsDoesNotThrow) {
    EXPECT_NO_THROW(check(true, "value={}", 42));
}

// ─── check() fails when expression is false ─────────────────────────────────

TEST(LoggingTest, CheckFalseThrowsRuntimeError) {
    EXPECT_THROW(check(false, "assertion failed: {}", "bad state"), std::runtime_error);
}

TEST(LoggingTest, CheckFalseExceptionContainsMessage) {
    try {
        check(false, "check detail: {}", 456);
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error &e) {
        auto what = std::string_view{e.what()};
        EXPECT_NE(what.find("ERROR"), std::string_view::npos);
        EXPECT_NE(what.find("check detail: 456"), std::string_view::npos);
    }
}

TEST(LoggingTest, CheckFalseExceptionContainsSourceLocation) {
    try {
        check(false, "loc in check");
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error &e) {
        auto what = std::string_view{e.what()};
        EXPECT_NE(what.find("logging_test.cpp"), std::string_view::npos);
    }
}

// ─── check() with no format args ────────────────────────────────────────────

TEST(LoggingTest, CheckFalseNoArgs) {
    EXPECT_THROW(check(false, "simple check failed"), std::runtime_error);
}

// ─── LogFormatString captures source_location ───────────────────────────────

TEST(LoggingTest, LogFormatStringCapturesLocation) {
    auto fmt = detail::LogFormatString<LogConfig, int>{"val={}", std::source_location::current()};
    EXPECT_NE(std::string_view{fmt.loc.file_name()}.find("logging_test.cpp"), std::string_view::npos);
    EXPECT_GT(fmt.loc.line(), 0u);
}

// ─── Newline termination ────────────────────────────────────────────────────

TEST(LoggingTest, LogOutputEndsWithNewline) {
    auto output = capture_stderr([] { log<LogLevel::LL_INFO>("newline test"); });
    ASSERT_FALSE(output.empty());
    EXPECT_EQ(output.back(), '\n');
}

TEST(LoggingTest, ErrorExceptionEndsWithNewline) {
    try {
        log<LogLevel::LL_ERROR>("error newline");
        FAIL();
    } catch (const std::runtime_error &e) {
        auto what = std::string_view{e.what()};
        ASSERT_FALSE(what.empty());
        EXPECT_EQ(what.back(), '\n');
    }
}

// ─── log() with no format string or args ─────────────────────────────────────

TEST(LoggingTest, LogNoArgsWritesToStderr) {
    const auto output = capture_stderr([] { EXPECT_THROW(log<LogLevel::LL_ERROR>(), std::runtime_error); });
    EXPECT_NE(output.find("ERROR"), std::string::npos);
    EXPECT_NE(output.find("logging_test.cpp"), std::string::npos);
}

TEST(LoggingTest, LogNoArgsContainsPidAndTid) {
    auto output = capture_stderr([] { log<LogLevel::LL_WARNING>(); });
    auto pid_str = std::string{"pid:"} + std::to_string(getpid());
    EXPECT_NE(output.find(pid_str), std::string::npos);
    EXPECT_NE(output.find("tid:"), std::string::npos);
}

TEST(LoggingTest, LogNoArgsEndsWithNewline) {
    const auto output = capture_stderr([] { EXPECT_THROW(log<LogLevel::LL_ERROR>(), std::runtime_error); });
    ASSERT_FALSE(output.empty());
    EXPECT_EQ(output.back(), '\n');
}

TEST(LoggingTest, LogNoArgsFatalCallsTerminate) {
    EXPECT_EXIT(
        {
            std::set_terminate([] { std::_Exit(73); });
            log<LogLevel::LL_FATAL>();
        },
        ::testing::ExitedWithCode(73), "FATAL");
}

TEST(LoggingTest, LogNoArgsFatalContainsSourceLocation) {
    EXPECT_EXIT(
        {
            std::set_terminate([] { std::_Exit(73); });
            log<LogLevel::LL_FATAL>();
        },
        ::testing::ExitedWithCode(73), "logging_test.cpp");
}

// ─── check() with no format string or args ───────────────────────────────────

TEST(LoggingTest, CheckNoArgsTrueDoesNotThrow) {
    EXPECT_NO_THROW(check(true));
}

TEST(LoggingTest, CheckNoArgsFalseThrows) {
    EXPECT_THROW(check(false), std::runtime_error);
}

TEST(LoggingTest, CheckNoArgsFalseExceptionContainsLocation) {
    try {
        check(false);
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error &e) {
        auto what = std::string_view{e.what()};
        EXPECT_NE(what.find("ERROR"), std::string_view::npos);
        EXPECT_NE(what.find("logging_test.cpp"), std::string_view::npos);
    }
}

// ─── assert_check() ─────────────────────────────────────────────────────────

TEST(LoggingTest, AssertionAndInputChecksHaveDistinctExceptionContracts) {
    static_assert(noexcept(assert_check([] { return true; })));
    static_assert(noexcept(assert_check([] { return true; }, "invariant {}", 1)));
    static_assert(!noexcept(check(true)));
    static_assert(!noexcept(check(true, "input {}", 1)));
}

TEST(LoggingTest, AssertCheckTerminatesWhenPredicateThrows) {
    const auto predicate = []() -> bool {
        throw std::runtime_error{"broken invariant predicate"};
    };
    if constexpr (compile_config::is_debug_build) {
        EXPECT_EXIT(
            {
                prepare_assertion_death_test();
                assert_check(predicate, "internal invariant");
            },
            ::testing::ExitedWithCode(73), "");
    } else {
        EXPECT_NO_THROW(assert_check(predicate, "internal invariant"));
    }
}

TEST(LoggingTest, AssertCheckWithoutMessageTerminatesWhenPredicateThrows) {
    const auto predicate = []() -> bool {
        throw std::runtime_error{"broken invariant predicate"};
    };
    if constexpr (compile_config::is_debug_build) {
        EXPECT_EXIT(
            {
                prepare_assertion_death_test();
                assert_check(predicate);
            },
            ::testing::ExitedWithCode(73), "");
    } else {
        EXPECT_NO_THROW(assert_check(predicate));
    }
}

TEST(LoggingTest, CheckPropagatesPredicateExceptions) {
    const auto predicate = []() -> bool {
        throw std::runtime_error{"input validation failed"};
    };
    EXPECT_THROW(check(predicate), std::runtime_error);
    EXPECT_THROW(check(predicate, "external input"), std::runtime_error);
}

TEST(LoggingTest, AssertCheckTrueDoesNotThrow) {
    EXPECT_NO_THROW(assert_check([] { return true; }, "should not fire"));
}

TEST(LoggingTest, AssertCheckTrueWithArgsDoesNotThrow) {
    EXPECT_NO_THROW(assert_check([] { return true; }, "value={}", 42));
}

TEST(LoggingTest, AssertCheckFalseTerminatesInDebug) {
#ifndef NDEBUG
    EXPECT_EXIT(
        {
            prepare_assertion_death_test();
            assert_check([] { return false; }, "assert failed: {}", "bad");
        },
        ::testing::ExitedWithCode(73), "assert failed: bad");
#endif
}

TEST(LoggingTest, AssertCheckFalseDiagnosticContainsMessage) {
#ifndef NDEBUG
    EXPECT_EXIT(
        {
            prepare_assertion_death_test();
            assert_check([] { return false; }, "assert detail: {}", 789);
        },
        ::testing::ExitedWithCode(73), "FATAL.*assert detail: 789");
#endif
}

TEST(LoggingTest, AssertCheckFalseDiagnosticContainsSourceLocation) {
#ifndef NDEBUG
    EXPECT_EXIT(
        {
            prepare_assertion_death_test();
            assert_check([] { return false; }, "loc in assert_check");
        },
        ::testing::ExitedWithCode(73), "logging_test.cpp");
#endif
}

// ─── assert_check() with no format string or args ────────────────────────────

TEST(LoggingTest, AssertCheckNoArgsTrueDoesNotThrow) {
    EXPECT_NO_THROW(assert_check([] { return true; }));
}

TEST(LoggingTest, AssertCheckNoArgsFalseTerminatesInDebug) {
#ifndef NDEBUG
    EXPECT_EXIT(
        {
            prepare_assertion_death_test();
            assert_check([] { return false; });
        },
        ::testing::ExitedWithCode(73), "FATAL");
#endif
}

TEST(LoggingTest, AssertCheckNoArgsFalseDiagnosticContainsLocation) {
#ifndef NDEBUG
    EXPECT_EXIT(
        {
            prepare_assertion_death_test();
            assert_check([] { return false; });
        },
        ::testing::ExitedWithCode(73), "FATAL.*logging_test.cpp");
#endif
}

TEST(LoggingTest, AssertCheckPredicateNotCalledInRelease) {
    auto called = false;
    auto predicate = [&called] {
        called = true;
        return true;
    };
    assert_check(predicate, "should not matter");
#ifdef NDEBUG
    EXPECT_FALSE(called);
#else
    EXPECT_TRUE(called);
#endif
}

TEST(LoggingTest, PrintAllColor) {
    log<LogLevel::LL_DEBUG>();
    EXPECT_THROW(log<LogLevel::LL_ERROR>(), std::runtime_error);
    log<LogLevel::LL_INFO>();
    log<LogLevel::LL_WARNING>();
    EXPECT_EXIT(
        {
            std::set_terminate([] { std::_Exit(73); });
            log<LogLevel::LL_FATAL>();
        },
        ::testing::ExitedWithCode(73), "FATAL");
}

TEST(LoggingTest, ErrorUsesConfiguredException) {
    static_assert(LogException<ProjectError>);
    static_assert(LogConfiguration<ProjectLogConfig>);
    EXPECT_THROW((log<LogLevel::LL_ERROR, ProjectLogConfig>("project error: {}", 42)), ProjectError);
}

TEST(LoggingTest, LogExceptionRejectsInvalidExceptionTypes) {
    static_assert(!LogException<std::string>);
    static_assert(!LogException<std::exception>);
}

TEST(LoggingTest, LoggingApisRejectInvalidConfigurations) {
    constexpr auto rejected = []<typename Config>() {
        return !requires { log<LogLevel::LL_ERROR, Config>(); } && !requires {
            log<LogLevel::LL_ERROR, Config>("invalid config");
        } && !requires { check<Config>(true); } && !requires { check<Config>(true, "invalid config"); } && !requires {
            assert_check<Config>([] { return true; });
        } && !requires { assert_check<Config>([] { return true; }, "invalid config"); };
    };
    static_assert(rejected.template operator()<MissingExceptionConfig>());
    static_assert(rejected.template operator()<MissingSourceLocationConfig>());
    static_assert(rejected.template operator()<InvalidExceptionConfig>());
    static_assert(rejected.template operator()<IntegerSourceLocationConfig>());
    static_assert(rejected.template operator()<NonConstantSourceLocationConfig>());
    static_assert(rejected.template operator()<InvalidLogLevelConfig>());
    static_assert(rejected.template operator()<NonConstantLogLevelConfig>());
    static_assert(rejected.template operator()<EmptyBufferLogConfig>());
    static_assert(rejected.template operator()<NonConstantBufferLogConfig>());
}

TEST(LoggingTest, AssertCheckRejectsInvalidPredicates) {
    constexpr auto rejected = []<typename Predicate>() {
        return !requires(Predicate &&predicate) { assert_check(std::forward<Predicate>(predicate)); }
               && !requires(Predicate &&predicate) {
                      assert_check(std::forward<Predicate>(predicate), "invalid predicate");
                  };
    };
    static_assert(rejected.template operator()<bool>());
    static_assert(rejected.template operator()<decltype([] {})>());
    static_assert(rejected.template operator()<decltype([](int) { return true; })>());
    static_assert(rejected.template operator()<decltype([] { return std::string{}; })>());
}

TEST(LoggingTest, ErrorWithoutMessageUsesConfiguredException) {
    EXPECT_THROW((log<LogLevel::LL_ERROR, ProjectLogConfig>()), ProjectError);
}

TEST(LoggingTest, CheckUsesConfiguredException) {
    EXPECT_THROW(check<ProjectLogConfig>(false, "project check: {}", 42), ProjectError);
}

TEST(LoggingTest, CheckWithoutMessageUsesConfiguredException) {
    EXPECT_THROW(check<ProjectLogConfig>(false), ProjectError);
}

TEST(LoggingTest, PrivateLogOmitsSourceLocation) {
    const auto output = capture_stderr([] { log<LogLevel::LL_INFO, PrivateLogConfig>("private message"); });
    EXPECT_NE(output.find("private message"), std::string::npos);
    EXPECT_EQ(output.find("logging_test.cpp"), std::string::npos);
    EXPECT_EQ(output.find("\033[35m"), std::string::npos);
    EXPECT_EQ(output.find(":0 "), std::string::npos);
}

TEST(LoggingTest, PrivateLogWithoutMessageOmitsSourceLocation) {
    const auto output = capture_stderr([] { log<LogLevel::LL_INFO, PrivateLogConfig>(); });
    EXPECT_NE(output.find("INFO"), std::string::npos);
    EXPECT_EQ(output.find("logging_test.cpp"), std::string::npos);
    EXPECT_EQ(output.find(":0 "), std::string::npos);
}

TEST(LoggingTest, PrivateErrorOmitsSourceLocationFromOutputAndException) {
    const auto output = capture_stderr([] {
        try {
            log<LogLevel::LL_ERROR, PrivateLogConfig>("private error: {}", 42);
            FAIL() << "Expected ProjectError";
        } catch (const ProjectError &e) {
            const auto message = std::string_view{e.what()};
            EXPECT_NE(message.find("<ERROR> private error: 42\n"), std::string_view::npos);
            EXPECT_EQ(message.find("logging_test.cpp"), std::string_view::npos);
            EXPECT_EQ(message.find("\033["), std::string_view::npos);
        }
    });
    EXPECT_NE(output.find("private error: 42"), std::string::npos);
    EXPECT_EQ(output.find("logging_test.cpp"), std::string::npos);
}

TEST(LoggingTest, PrivateCheckOmitsSourceLocation) {
    try {
        check<PrivateLogConfig>(false, "private check");
        FAIL() << "Expected ProjectError";
    } catch (const ProjectError &e) {
        const auto message = std::string_view{e.what()};
        EXPECT_NE(message.find("<ERROR> private check\n"), std::string_view::npos);
        EXPECT_EQ(message.find("logging_test.cpp"), std::string_view::npos);
    }
}

TEST(LoggingTest, PrivateCheckWithoutMessageOmitsSourceLocation) {
    try {
        check<PrivateLogConfig>(false);
        FAIL() << "Expected ProjectError";
    } catch (const ProjectError &e) {
        const auto message = std::string_view{e.what()};
        EXPECT_NE(message.find("<ERROR> \n"), std::string_view::npos);
        EXPECT_EQ(message.find("logging_test.cpp"), std::string_view::npos);
    }
}

TEST(LoggingTest, ErrorRetainsCallerLineWhenSourceLocationIsEnabled) {
    const auto expected_line = std::source_location::current().line() + 2;
    try {
        log<LogLevel::LL_ERROR>("caller location");
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error &e) {
        EXPECT_NE(std::string_view{e.what()}.find(std::format("logging_test.cpp:{} ", expected_line)),
                  std::string_view::npos);
    }
}

TEST(LoggingTest, LogDebugSkipsLazyArgumentsInRelease) {
    auto calls = 0;
    const auto output = capture_stderr([&] { log<LogLevel::LL_DEBUG>("lazy debug: {}", [&] { return ++calls; }); });
    if constexpr (compile_config::is_debug_build) {
        EXPECT_EQ(calls, 1);
        EXPECT_NE(output.find("lazy debug: 1"), std::string::npos);
    } else {
        EXPECT_EQ(calls, 0);
        EXPECT_TRUE(output.empty());
    }
}

TEST(LoggingTest, LogInfoEvaluatesLazyArgumentsOnce) {
    auto calls = 0;
    const auto output = capture_stderr([&] { log<LogLevel::LL_INFO>("lazy info: {}", [&] { return ++calls; }); });
    EXPECT_EQ(calls, 1);
    EXPECT_NE(output.find("lazy info: 1"), std::string::npos);
}

TEST(LoggingTest, LogErrorEvaluatesLazyArgumentsOnce) {
    auto calls = 0;
    try {
        log<LogLevel::LL_ERROR>("lazy error: {}", [&] { return ++calls; });
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error &e) {
        EXPECT_NE(std::string_view{e.what()}.find("lazy error: 1"), std::string_view::npos);
    }
    EXPECT_EQ(calls, 1);
}

TEST(LoggingTest, LogAcceptsMoveOnlyLazyProducer) {
    const auto output = capture_stderr([] {
        auto producer = [value = std::make_unique<int>(42)] {
            return *value;
        };
        log<LogLevel::LL_INFO>("move-only producer: {}", std::move(producer));
    });
    EXPECT_NE(output.find("move-only producer: 42"), std::string::npos);
}

TEST(LoggingTest, LogFormatsLazyLvalueReference) {
    const auto value = std::string{"referenced value"};
    const auto output =
        capture_stderr([&] { log<LogLevel::LL_INFO>("{}", [&]() -> const std::string & { return value; }); });
    EXPECT_NE(output.find(value), std::string::npos);
}

TEST(LoggingTest, LogFormatsLazyRvalueReference) {
    auto value = std::string{"rvalue reference"};
    const auto output =
        capture_stderr([&] { log<LogLevel::LL_INFO>("{}", [&]() -> std::string && { return std::move(value); }); });
    EXPECT_NE(output.find("rvalue reference"), std::string::npos);
}

TEST(LoggingTest, CheckTrueSkipsLazyArguments) {
    auto calls = 0;
    EXPECT_NO_THROW(check(true, "unused: {}", [&] { return ++calls; }));
    EXPECT_EQ(calls, 0);
}

TEST(LoggingTest, CheckEvaluatesPredicateOnce) {
    auto calls = 0;
    EXPECT_NO_THROW(check([&] { return ++calls == 1; }, "predicate failed"));
    EXPECT_EQ(calls, 1);
}

TEST(LoggingTest, CheckWithoutMessageEvaluatesPredicateOnce) {
    auto calls = 0;
    EXPECT_THROW(check([&] { return ++calls == 0; }), std::runtime_error);
    EXPECT_EQ(calls, 1);
}

TEST(LoggingTest, CheckFalseEvaluatesLazyArgumentsOnce) {
    auto calls = 0;
    try {
        check(false, "lazy check: {}", [&] { return ++calls; });
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error &e) {
        EXPECT_NE(std::string_view{e.what()}.find("lazy check: 1"), std::string_view::npos);
    }
    EXPECT_EQ(calls, 1);
}

TEST(LoggingTest, AssertCheckTrueSkipsLazyArguments) {
    auto calls = 0;
    EXPECT_NO_THROW(assert_check([] { return true; }, "unused: {}", [&] { return ++calls; }));
    EXPECT_EQ(calls, 0);
}

TEST(LoggingTest, AssertCheckSkipsPredicateAndLazyArgumentsInRelease) {
    auto predicate_calls = 0;
    auto argument_calls = 0;
    const auto predicate = [&] {
        return ++predicate_calls == 0;
    };
    const auto argument = [&] {
        return ++argument_calls;
    };
    if constexpr (compile_config::is_debug_build) {
        EXPECT_EXIT(
            {
                prepare_assertion_death_test();
                assert_check<PrivateLogConfig>(predicate, "lazy assert: {}, predicate calls: {}", argument,
                                               [&] { return predicate_calls; });
            },
            ::testing::ExitedWithCode(73), "lazy assert: 1, predicate calls: 1");
    } else {
        EXPECT_NO_THROW(assert_check<PrivateLogConfig>(predicate, "lazy assert: {}", argument));
        EXPECT_EQ(predicate_calls, 0);
        EXPECT_EQ(argument_calls, 0);
    }
}

TEST(LoggingTest, AssertCheckTerminatesWhenLazyArgumentThrows) {
    if constexpr (compile_config::is_debug_build) {
        EXPECT_EXIT(
            {
                prepare_assertion_death_test();
                assert_check([] { return false; }, "{}",
                             []() -> int { throw std::runtime_error{"lazy diagnostic failed"}; });
            },
            ::testing::ExitedWithCode(73), "");
    }
}

TEST(LoggingTest, AssertCheckTerminatesWithConfiguredThreshold) {
    if constexpr (compile_config::is_debug_build) {
        EXPECT_EXIT(
            {
                prepare_assertion_death_test();
                assert_check<HighThresholdLogConfig>([] { return false; }, "assertion remains enabled");
            },
            ::testing::ExitedWithCode(73), "FATAL.*assertion remains enabled");
    }
}

TEST(LoggingTest, AssertCheckNoArgsSkipsPredicateInRelease) {
    auto calls = 0;
    assert_check([&] { return ++calls == 1; });
    EXPECT_EQ(calls, compile_config::is_debug_build ? 1 : 0);
}

TEST(LoggingTest, AssertCheckSupportsConstantEvaluation) {
    constexpr auto valid = [] {
        assert_check([] { return true; }, "constant assertion: {}", 42);
        assert_check([] { return true; });
        return true;
    }();
    static_assert(valid);
    EXPECT_TRUE(valid);
}

TEST(LoggingTest, LogFatalTerminatesWhenLazyArgumentThrows) {
    EXPECT_EXIT(
        {
            std::set_terminate([] { std::_Exit(73); });
            log<LogLevel::LL_FATAL>("{}", []() -> int { throw std::runtime_error{"lazy diagnostic failed"}; });
        },
        ::testing::ExitedWithCode(73), "");
}

TEST(LoggingTest, ConfiguredThresholdSkipsLazyOrdinaryLogs) {
    auto calls = 0;
    const auto output = capture_stderr([&] {
        log<LogLevel::LL_DEBUG, HighThresholdLogConfig>("{}", [&] { return ++calls; });
        log<LogLevel::LL_INFO, HighThresholdLogConfig>("{}", [&] { return ++calls; });
        log<LogLevel::LL_WARNING, HighThresholdLogConfig>("{}", [&] { return ++calls; });
    });
    EXPECT_EQ(calls, 0);
    EXPECT_TRUE(output.empty());
}

TEST(LoggingTest, ErrorThrowsRegardlessOfConfiguredThreshold) {
    auto calls = 0;
    const auto output = capture_stderr([&] {
        EXPECT_THROW((log<LogLevel::LL_ERROR, HighThresholdLogConfig>("error: {}", [&] { return ++calls; })),
                     ProjectError);
    });
    EXPECT_EQ(calls, 1);
    EXPECT_NE(output.find("error: 1"), std::string::npos);
}

TEST(LoggingTest, FailedCheckThrowsRegardlessOfConfiguredThreshold) {
    EXPECT_THROW(check<HighThresholdLogConfig>(false, "check remains enabled"), ProjectError);
}

TEST(LoggingTest, FatalTerminatesWithConfiguredThreshold) {
    EXPECT_EXIT(
        {
            std::set_terminate([] { std::_Exit(73); });
            (log<LogLevel::LL_FATAL, HighThresholdLogConfig>("fatal remains enabled"));
        },
        ::testing::ExitedWithCode(73), "fatal remains enabled");
}

TEST(LoggingTest, ExplicitDebugThresholdEnablesDebugInBothBuildModes) {
    const auto output = capture_stderr([] { log<LogLevel::LL_DEBUG, SmallBufferLogConfig>("explicit debug"); });
    EXPECT_NE(output.find("explicit debug"), std::string::npos);
}

TEST(LoggingTest, LogTruncatesAtConfiguredBufferCapacity) {
    const auto message = std::string(256, 'x');
    const auto output = capture_stderr([&] { log<LogLevel::LL_INFO, SmallBufferLogConfig>("{}", message); });
    ASSERT_EQ(output.size(), SmallBufferLogConfig::buffer_size);
    EXPECT_EQ(output.back(), '\n');
}

TEST(LoggingTest, LargerConfiguredBufferPreservesLongMessages) {
    const auto message = std::string(600, 'x');
    const auto output = capture_stderr([&] { log<LogLevel::LL_INFO, LargeBufferLogConfig>("{}", message); });
    EXPECT_NE(output.find(message), std::string::npos);
}

TEST(LoggingTest, ErrorUsesConfiguredBufferCapacity) {
    const auto message = std::string(256, 'x');
    const auto output = capture_stderr([&] {
        try {
            log<LogLevel::LL_ERROR, SmallBufferLogConfig>("{}", message);
            FAIL() << "Expected ProjectError";
        } catch (const ProjectError &e) {
            const auto diagnostic = std::string_view{e.what()};
            ASSERT_FALSE(diagnostic.empty());
            EXPECT_LE(diagnostic.size(), SmallBufferLogConfig::buffer_size);
            EXPECT_EQ(diagnostic.back(), '\n');
        }
    });
    ASSERT_EQ(output.size(), SmallBufferLogConfig::buffer_size);
    EXPECT_EQ(output.back(), '\n');
}
