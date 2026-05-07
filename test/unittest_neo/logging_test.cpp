#include "gtest/gtest.h"
#include "neo/Logging.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unistd.h>

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

TEST(LoggingTest, FatalExceptionMessageHasNoColorCodes) {
    try {
        log<LogLevel::LL_FATAL>("no color in exception");
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
    EXPECT_EQ(detail::kCompileTimeMinLevel, LogLevel::LL_FATAL);
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
#endif
}

TEST(LoggingTest, LogInfoWritesToStderr) {
    auto output = capture_stderr([] { log<LogLevel::LL_INFO>("info msg: {}", "test_value"); });
#ifndef NDEBUG
    EXPECT_NE(output.find("INFO"), std::string::npos);
    EXPECT_NE(output.find("info msg: test_value"), std::string::npos);
#endif
}

TEST(LoggingTest, LogWarningWritesToStderr) {
    auto output = capture_stderr([] { log<LogLevel::LL_WARNING>("warn: {}", 99); });
#ifndef NDEBUG
    EXPECT_NE(output.find("WARN"), std::string::npos);
    EXPECT_NE(output.find("warn: 99"), std::string::npos);
#endif
}

TEST(LoggingTest, LogErrorWritesToStderr) {
    auto output = capture_stderr([] { log<LogLevel::LL_ERROR>("error code={}", 500); });
#ifndef NDEBUG
    EXPECT_NE(output.find("ERROR"), std::string::npos);
    EXPECT_NE(output.find("error code=500"), std::string::npos);
#endif
}

// ─── log() fatal level throws runtime_error ─────────────────────────────────

TEST(LoggingTest, LogFatalThrowsRuntimeError) {
    EXPECT_THROW(log<LogLevel::LL_FATAL>("fatal: {}", "crash"), std::runtime_error);
}

TEST(LoggingTest, LogFatalExceptionContainsMessage) {
    try {
        log<LogLevel::LL_FATAL>("fatal detail: {}", 123);
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error &e) {
        auto what = std::string_view{e.what()};
        EXPECT_NE(what.find("FATAL"), std::string_view::npos);
        EXPECT_NE(what.find("fatal detail: 123"), std::string_view::npos);
    }
}

// ─── log() output contains source location ──────────────────────────────────

TEST(LoggingTest, LogOutputContainsSourceFile) {
    auto output = capture_stderr([] { log<LogLevel::LL_ERROR>("loc check"); });
#ifndef NDEBUG
    // Should contain the filename of this test file
    EXPECT_NE(output.find("logging_test.cpp"), std::string::npos);
#endif
}

// ─── log() output contains PID and TID ──────────────────────────────────────

TEST(LoggingTest, LogOutputContainsPid) {
    auto output = capture_stderr([] { log<LogLevel::LL_ERROR>("pid check"); });
#ifndef NDEBUG
    auto pid_str = std::string{"pid:"} + std::to_string(getpid());
    EXPECT_NE(output.find(pid_str), std::string::npos);
#endif
}

TEST(LoggingTest, LogOutputContainsTid) {
    auto output = capture_stderr([] { log<LogLevel::LL_ERROR>("tid check"); });
#ifndef NDEBUG
    EXPECT_NE(output.find("tid:"), std::string::npos);
#endif
}

// ─── log() output contains timestamp ────────────────────────────────────────

TEST(LoggingTest, LogOutputContainsTimestamp) {
    auto output = capture_stderr([] { log<LogLevel::LL_ERROR>("time check"); });
#ifndef NDEBUG
    // Should contain year prefix "20"
    EXPECT_NE(output.find("20"), std::string::npos);
#endif
}

// ─── log() with no format args ──────────────────────────────────────────────

TEST(LoggingTest, LogWithNoArgs) {
    auto output = capture_stderr([] { log<LogLevel::LL_ERROR>("plain message no args"); });
#ifndef NDEBUG
    EXPECT_NE(output.find("plain message no args"), std::string::npos);
#endif
}

// ─── log() with multiple args ───────────────────────────────────────────────

TEST(LoggingTest, LogWithMultipleArgs) {
    auto output = capture_stderr([] { log<LogLevel::LL_ERROR>("a={} b={} c={}", 1, "two", 3.0); });
#ifndef NDEBUG
    EXPECT_NE(output.find("a=1 b=two c=3"), std::string::npos);
#endif
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
        EXPECT_NE(what.find("FATAL"), std::string_view::npos);
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
    auto fmt = detail::LogFormatString<int>{"val={}", std::source_location::current()};
    EXPECT_NE(std::string_view{fmt.loc.file_name()}.find("logging_test.cpp"), std::string_view::npos);
    EXPECT_GT(fmt.loc.line(), 0u);
}

// ─── Newline termination ────────────────────────────────────────────────────

TEST(LoggingTest, LogOutputEndsWithNewline) {
    auto output = capture_stderr([] { log<LogLevel::LL_ERROR>("newline test"); });
#ifndef NDEBUG
    ASSERT_FALSE(output.empty());
    EXPECT_EQ(output.back(), '\n');
#endif
}

TEST(LoggingTest, FatalExceptionEndsWithNewline) {
    try {
        log<LogLevel::LL_FATAL>("fatal newline");
        FAIL();
    } catch (const std::runtime_error &e) {
        auto what = std::string_view{e.what()};
        ASSERT_FALSE(what.empty());
        EXPECT_EQ(what.back(), '\n');
    }
}

// ─── log() with no format string or args ─────────────────────────────────────

TEST(LoggingTest, LogNoArgsWritesToStderr) {
    auto output = capture_stderr([] { log<LogLevel::LL_ERROR>(); });
#ifndef NDEBUG
    EXPECT_NE(output.find("ERROR"), std::string::npos);
    EXPECT_NE(output.find("logging_test.cpp"), std::string::npos);
#endif
}

TEST(LoggingTest, LogNoArgsContainsPidAndTid) {
    auto output = capture_stderr([] { log<LogLevel::LL_WARNING>(); });
#ifndef NDEBUG
    auto pid_str = std::string{"pid:"} + std::to_string(getpid());
    EXPECT_NE(output.find(pid_str), std::string::npos);
    EXPECT_NE(output.find("tid:"), std::string::npos);
#endif
}

TEST(LoggingTest, LogNoArgsEndsWithNewline) {
    auto output = capture_stderr([] { log<LogLevel::LL_ERROR>(); });
#ifndef NDEBUG
    ASSERT_FALSE(output.empty());
    EXPECT_EQ(output.back(), '\n');
#endif
}

TEST(LoggingTest, LogNoArgsFatalThrows) {
    EXPECT_THROW(log<LogLevel::LL_FATAL>(), std::runtime_error);
}

TEST(LoggingTest, LogNoArgsFatalExceptionContainsLevel) {
    try {
        log<LogLevel::LL_FATAL>();
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error &e) {
        auto what = std::string_view{e.what()};
        EXPECT_NE(what.find("FATAL"), std::string_view::npos);
        EXPECT_NE(what.find("logging_test.cpp"), std::string_view::npos);
    }
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
        EXPECT_NE(what.find("FATAL"), std::string_view::npos);
        EXPECT_NE(what.find("logging_test.cpp"), std::string_view::npos);
    }
}

// ─── assert_check() ─────────────────────────────────────────────────────────

TEST(LoggingTest, AssertCheckTrueDoesNotThrow) {
    EXPECT_NO_THROW(assert_check([] { return true; }, "should not fire"));
}

TEST(LoggingTest, AssertCheckTrueWithArgsDoesNotThrow) {
    EXPECT_NO_THROW(assert_check([] { return true; }, "value={}", 42));
}

TEST(LoggingTest, AssertCheckFalseThrowsInDebug) {
#ifndef NDEBUG
    EXPECT_THROW(assert_check([] { return false; }, "assert failed: {}", "bad"), std::runtime_error);
#endif
}

TEST(LoggingTest, AssertCheckFalseExceptionContainsMessage) {
#ifndef NDEBUG
    try {
        assert_check([] { return false; }, "assert detail: {}", 789);
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error &e) {
        auto what = std::string_view{e.what()};
        EXPECT_NE(what.find("FATAL"), std::string_view::npos);
        EXPECT_NE(what.find("assert detail: 789"), std::string_view::npos);
    }
#endif
}

TEST(LoggingTest, AssertCheckFalseExceptionContainsSourceLocation) {
#ifndef NDEBUG
    try {
        assert_check([] { return false; }, "loc in assert_check");
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error &e) {
        auto what = std::string_view{e.what()};
        EXPECT_NE(what.find("logging_test.cpp"), std::string_view::npos);
    }
#endif
}

// ─── assert_check() with no format string or args ────────────────────────────

TEST(LoggingTest, AssertCheckNoArgsTrueDoesNotThrow) {
    EXPECT_NO_THROW(assert_check([] { return true; }));
}

TEST(LoggingTest, AssertCheckNoArgsFalseThrowsInDebug) {
#ifndef NDEBUG
    EXPECT_THROW(assert_check([] { return false; }), std::runtime_error);
#endif
}

TEST(LoggingTest, AssertCheckNoArgsFalseExceptionContainsLocation) {
#ifndef NDEBUG
    try {
        assert_check([] { return false; });
        FAIL() << "Expected std::runtime_error";
    } catch (const std::runtime_error &e) {
        auto what = std::string_view{e.what()};
        EXPECT_NE(what.find("FATAL"), std::string_view::npos);
        EXPECT_NE(what.find("logging_test.cpp"), std::string_view::npos);
    }
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
    try {
        log<LogLevel::LL_DEBUG>();
        log<LogLevel::LL_ERROR>();
        log<LogLevel::LL_INFO>();
        log<LogLevel::LL_WARNING>();
        log<LogLevel::LL_FATAL>();
    } catch (const std::runtime_error &e) {
        
    }
}