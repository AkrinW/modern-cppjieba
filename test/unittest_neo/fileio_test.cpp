#include "gtest/gtest.h"
#include "neo/detail/FileIO.hpp"
#include "neo/detail/StringUtil.hpp"

#include "test_paths.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

#include <sys/resource.h>
#include <sys/stat.h>

using namespace neo_cppjieba;

TEST(StringUtilTest, DecodeValueConsumesTheEntireInteger) {
    const auto invalid = std::array<std::string_view, 4>{"12abc", "12 ", "12.5", std::string_view{"12\0", 3}};
    for (const auto input : invalid) {
        EXPECT_THROW(decode_value<int>(input), LogConfig::Exception) << input;
    }
}

TEST(StringUtilTest, DecodeValueConsumesTheEntireFloat) {
    EXPECT_THROW(decode_value<double>("1.25x"), LogConfig::Exception);
}

TEST(StringUtilTest, DecodeValueRejectsEmptyInput) {
    EXPECT_THROW(decode_value<int>(std::string_view{}), LogConfig::Exception);
    EXPECT_THROW(decode_value<double>(""), LogConfig::Exception);
}

TEST(StringUtilTest, DecodeValueRejectsOutOfRangeInput) {
    EXPECT_THROW(decode_value<int64_t>("9223372036854775808"), LogConfig::Exception);
    EXPECT_THROW(decode_value<double>("1e10000"), LogConfig::Exception);
}

TEST(StringUtilTest, DecodeValueAcceptsCompleteNumbers) {
    EXPECT_EQ(decode_value<int>("-42"), -42);
    EXPECT_EQ(decode_value<uint64_t>("18446744073709551615"), std::numeric_limits<uint64_t>::max());
    EXPECT_DOUBLE_EQ(decode_value<double>("1.25e-2"), 0.0125);
}

TEST(StringUtilTest, EncodeValueRoundTripsNumericLimits) {
    for (const auto value : {std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max()}) {
        EXPECT_EQ(decode_value<int64_t>(encode_value(value)), value);
    }
    for (const auto value : {std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max()}) {
        EXPECT_DOUBLE_EQ(decode_value<double>(encode_value(value)), value);
    }
}

TEST(StringUtilTest, SeparatorIndicesCountRunesAndIncludeEndSentinel) {
    const auto text = std::u32string_view{U"𠮷，中 。"};
    const auto runes = std::span<const char32_t>{text};
    const auto expected = std::vector<uint32_t>{1, 3, 4, 5};
    auto indices = std::vector<uint32_t>{99};
    get_pre_filter_separators(runes, indices);
    EXPECT_EQ(indices, expected);
    EXPECT_EQ(get_pre_filter_separators(runes), expected);
}

TEST(StringUtilTest, EmptySeparatorInputResetsOutputToEndSentinel) {
    const auto runes = std::span<const char32_t>{};
    const auto expected = std::vector<uint32_t>{0};
    auto indices = std::vector<uint32_t>{1, 2, 3};
    get_pre_filter_separators(runes, indices);
    EXPECT_EQ(indices, expected);
    EXPECT_EQ(get_pre_filter_separators(runes), expected);
}

// ─── lines_view tests ────────────────────────────────────────────────────────

TEST(LinesViewTest, BasicLines) {
    auto content = std::string_view{"hello\nworld\nfoo"};
    auto result = std::vector<std::string_view>{};
    auto lines = get_line_view(content);
    for (auto &&line : lines) {
        result.push_back(line);
    }
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], "hello");
    EXPECT_EQ(result[1], "world");
    EXPECT_EQ(result[2], "foo");
}

TEST(LinesViewTest, TrailingNewline) {
    // Trailing newline should NOT produce an extra empty element
    auto content = std::string_view{"a\nb\n"};
    auto result = std::vector<std::string_view>{};
    auto lines = get_line_view(content);
    for (auto &&line : lines) {
        result.push_back(line);
    }
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "b");
}

TEST(LinesViewTest, WindowsLineEndings) {
    auto content = std::string_view{"hello\r\nworld\r\n"};
    auto result = std::vector<std::string_view>{};
    auto lines = get_line_view(content);
    for (auto &&line : lines) {
        result.push_back(line);
    }
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], "hello");
    EXPECT_EQ(result[1], "world");
}

TEST(LinesViewTest, EmptyContent) {
    auto content = std::string_view{};
    auto result = std::vector<std::string_view>{};
    auto lines = get_line_view(content);
    for (auto &&line : lines) {
        result.push_back(line);
    }
    EXPECT_TRUE(result.empty());
}

TEST(LinesViewTest, SingleLineNoNewline) {
    auto content = std::string_view{"single"};
    auto result = std::vector<std::string_view>{};
    auto lines = get_line_view(content);
    for (auto &&line : lines) {
        result.push_back(line);
    }
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "single");
}

TEST(LinesViewTest, EmptyLines) {
    // "a\n\nb\n\n" -> "a", "", "b", ""
    // The last "\n" at the very end produces an empty remaining, which means the "" before it
    // is emitted, then the truly-empty remaining triggers done.
    auto content = std::string_view{"a\n\nb\n\n"};
    auto result = std::vector<std::string_view>{};
    auto lines = get_line_view(content);
    for (auto &&line : lines) {
        result.push_back(line);
    }
    ASSERT_EQ(result.size(), 4u);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "");
    EXPECT_EQ(result[2], "b");
    EXPECT_EQ(result[3], "");
}

TEST(LinesViewTest, OnlyNewlines) {
    auto content = std::string_view{"\n\n"};
    auto result = std::vector<std::string_view>{};
    auto lines = get_line_view(content);
    for (auto &&line : lines) {
        result.push_back(line);
    }
    // "\n\n" -> first advance: pos=0, current="", remaining="\n"
    //        -> second advance: pos=0, current="", remaining=""
    //        -> third advance: remaining.empty() -> done
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], "");
    EXPECT_EQ(result[1], "");
}

// ─── split_view tests ───────────────────────────────────────────────────────

TEST(SplitViewTest, BasicSplit) {
    auto line = std::string_view{"AT&T 3 nz"};
    auto result = std::vector<std::string_view>{};
    auto split = get_split_view(line, ' ');
    for (auto &&field : split) {
        result.push_back(field);
    }
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], "AT&T");
    EXPECT_EQ(result[1], "3");
    EXPECT_EQ(result[2], "nz");
}

TEST(SplitViewTest, SingleElement) {
    auto line = std::string_view{"word"};
    auto result = std::vector<std::string_view>{};
    auto split = get_split_view(line, ' ');
    for (auto &&field : split) {
        result.push_back(field);
    }
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "word");
}

TEST(SplitViewTest, EmptyInput) {
    const auto *empty = "";
    auto line = std::string_view{empty};
    auto result = std::vector<std::string_view>{};
    auto split = get_split_view(line, ' ');
    for (auto &&field : split) {
        result.push_back(field);
    }
    // Splitting empty string yields one empty element (standard behavior)
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], "");
}

TEST(SplitViewTest, ConsecutiveDelimiters) {
    auto line = std::string_view{"a,,b,,c"};
    auto result = std::vector<std::string_view>{};
    auto split = get_split_view(line, ',');
    for (auto &&field : split) {
        result.push_back(field);
    }
    ASSERT_EQ(result.size(), 5u);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "");
    EXPECT_EQ(result[2], "b");
    EXPECT_EQ(result[3], "");
    EXPECT_EQ(result[4], "c");
}

TEST(SplitViewTest, TrailingDelimiter) {
    auto line = std::string_view{"a,b,"};
    auto result = std::vector<std::string_view>{};
    auto split = get_split_view(line, ',');
    for (auto &&field : split) {
        result.push_back(field);
    }
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], "a");
    EXPECT_EQ(result[1], "b");
    EXPECT_EQ(result[2], "");
}

TEST(SplitViewTest, DictLineFormat) {
    // Simulate a jieba dict line: "word freq tag"
    auto line = std::string_view{"你好 12345 v"};
    auto result = std::vector<std::string_view>{};
    auto split = get_split_view(line, ' ');
    for (auto &&field : split) {
        result.push_back(field);
    }
    ASSERT_EQ(result.size(), 3u);
    EXPECT_EQ(result[0], "你好");
    EXPECT_EQ(result[1], "12345");
    EXPECT_EQ(result[2], "v");
}

// ─── FileBuffer tests ────────────────────────────────────────────────────────

namespace {

// Each file-buffer test owns an isolated directory, including files created before a failing assertion.
class FileBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto pattern = (std::filesystem::temp_directory_path() / "neo-file-buffer-XXXXXX").string();
        ASSERT_NE(::mkdtemp(pattern.data()), nullptr);
        directory_ = std::move(pattern);
    }

    void TearDown() override {
        if (!directory_.empty()) {
            auto error = std::error_code{};
            std::filesystem::remove_all(directory_, error);
            EXPECT_FALSE(error) << error.message();
        }
    }

    [[nodiscard]] auto file_path(std::string_view name) const -> std::string {
        return (directory_ / name).string();
    }

    void write_file(std::string_view name, std::string_view content) const {
        auto output = std::ofstream{file_path(name), std::ios::binary};
        ASSERT_TRUE(output.is_open());
        output << content;
        output.close();
        ASSERT_TRUE(output.good());
    }

    std::filesystem::path directory_;
};

} // namespace

TEST_F(FileBufferTest, OpenDictFile) {
    auto path = std::string(DICT_DIR) + "/jieba.dict.utf8";
    auto file = read_file(path);
    EXPECT_TRUE(file.is_open());
    EXPECT_GT(file.size(), 0u);
    // The dict file should start with a known entry
    auto content = file.content();
    EXPECT_FALSE(content.empty());
}

TEST_F(FileBufferTest, MoveSemantics) {
    auto path = std::string(DICT_DIR) + "/stop_words.utf8";
    auto file1 = read_file(path);
    auto size1 = file1.size();
    auto content1 = file1.content();

    // Move construct
    auto file2 = std::move(file1);
    EXPECT_EQ(file2.size(), size1);
    EXPECT_EQ(file2.content(), content1);
    // Moved-from should be empty
    EXPECT_EQ(file1.size(), 0u);
    EXPECT_FALSE(file1.is_open());
}

TEST_F(FileBufferTest, EmptyFileRemainsOpen) {
    ASSERT_NO_FATAL_FAILURE(write_file("empty", ""));
    const auto file = read_file(file_path("empty"));
    EXPECT_TRUE(file.is_open());
    EXPECT_EQ(file.size(), 0u);
    EXPECT_TRUE(file.content().empty());
}

TEST_F(FileBufferTest, MissingFileReportsPathAndSystemError) {
    const auto path = file_path("missing");
    try {
        const auto file = read_file(path);
        FAIL() << "Expected opening a missing file to throw";
    } catch (const LogConfig::Exception &error) {
        const auto message = std::string_view{error.what()};
        EXPECT_NE(message.find(path), std::string_view::npos);
        EXPECT_NE(message.find(std::error_code{ENOENT, std::generic_category()}.message()), std::string_view::npos);
    }
}

TEST_F(FileBufferTest, RejectsEmbeddedNullInsteadOfOpeningPathPrefix) {
    ASSERT_NO_FATAL_FAILURE(write_file("original", "original content"));
    auto path = file_path("original");
    path.push_back('\0');
    path.append("suffix");
    EXPECT_THROW(read_file(path), LogConfig::Exception);
}

TEST_F(FileBufferTest, RejectingDirectoryClosesDescriptor) {
    const auto path = directory_.string();
    const auto descriptor_before = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    ASSERT_NE(descriptor_before, -1);
    ASSERT_EQ(::close(descriptor_before), 0);

    EXPECT_THROW(read_file(path), LogConfig::Exception);

    // open reuses the lowest available descriptor; a failed constructor must leave it available.
    const auto descriptor_after = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    ASSERT_NE(descriptor_after, -1);
    EXPECT_EQ(descriptor_after, descriptor_before);
    EXPECT_EQ(::close(descriptor_after), 0);
}

TEST_F(FileBufferTest, RejectsFifoWithoutWaitingForWriter) {
    const auto path = file_path("fifo");
    ASSERT_EQ(::mkfifo(path.c_str(), S_IRUSR | S_IWUSR), 0);

    // Bound a regression to the subprocess so a blocking open cannot hang the test runner.
    EXPECT_EXIT(
        {
            ::alarm(5);
            try {
                const auto file = read_file(path);
            } catch (const LogConfig::Exception &) {
                std::_Exit(0);
            }
            std::_Exit(1);
        },
        ::testing::ExitedWithCode(0), "not a regular file");
}

TEST_F(FileBufferTest, AtomicReplacementPreservesLoadedContent) {
    ASSERT_NO_FATAL_FAILURE(write_file("published", "original content"));
    const auto path = file_path("published");
    const auto original = read_file(path);

    ASSERT_NO_FATAL_FAILURE(write_file("replacement", "replacement with a different size"));
    std::filesystem::rename(file_path("replacement"), path);

    EXPECT_EQ(original.content(), "original content");
    const auto replacement = read_file(path);
    EXPECT_EQ(replacement.content(), "replacement with a different size");
}

TEST_F(FileBufferTest, MoveAssignmentTransfersBufferAndEmptiesSource) {
    ASSERT_NO_FATAL_FAILURE(write_file("source", "source content"));
    ASSERT_NO_FATAL_FAILURE(write_file("destination", "old destination content"));
    auto source = read_file(file_path("source"));
    auto destination = read_file(file_path("destination"));
    const auto content = source.content();

    destination = std::move(source);

    EXPECT_TRUE(destination.is_open());
    EXPECT_EQ(destination.content(), "source content");
    EXPECT_EQ(destination.content().data(), content.data());
    EXPECT_FALSE(source.is_open());
    EXPECT_EQ(source.size(), 0u);
    EXPECT_TRUE(source.content().empty());
}

TEST_F(FileBufferTest, LoadedContentSurvivesExternalTruncation) {
    const auto expected = std::string(8192, 'x');
    ASSERT_NO_FATAL_FAILURE(write_file("truncate", expected));
    const auto path = file_path("truncate");

    // Isolate the old mmap SIGBUS regression so it cannot crash the whole test runner.
    EXPECT_EXIT(
        {
            const auto core_limit = ::rlimit{};
            if (::setrlimit(RLIMIT_CORE, &core_limit) != 0) {
                std::_Exit(2);
            }
            const auto file = read_file(path);
            const auto content = file.content();
            std::filesystem::resize_file(path, 0);
            std::_Exit(content == expected ? 0 : 1);
        },
        ::testing::ExitedWithCode(0), "");
}

TEST_F(FileBufferTest, LoadedContentSurvivesInPlaceOverwrite) {
    ASSERT_NO_FATAL_FAILURE(write_file("overwrite", "original"));
    const auto file = read_file(file_path("overwrite"));
    const auto content = file.content();

    ASSERT_NO_FATAL_FAILURE(write_file("overwrite", "modified"));

    EXPECT_EQ(content, "original");
    EXPECT_EQ(file.content(), "original");
}

TEST_F(FileBufferTest, TruncationDuringReadThrowsInsteadOfReturningPartialContent) {
    ASSERT_NO_FATAL_FAILURE(write_file("short-read", "12345678"));
    const auto path = file_path("short-read");
    const auto input = std::unique_ptr<std::FILE, decltype(&std::fclose)>{std::fopen(path.c_str(), "rb"), &std::fclose};
    ASSERT_NE(input, nullptr);

    // Simulate truncation after the original size was obtained, including a successful partial read before EOF.
    std::filesystem::resize_file(path, 3);
    auto destination = std::array<char, 8>{};
    try {
        detail::read_file_contents(::fileno(input.get()), std::span<char>{destination}, path);
        FAIL() << "Expected a truncated file to fail the read";
    } catch (const LogConfig::Exception &error) {
        const auto message = std::string_view{error.what()};
        EXPECT_NE(message.find("unexpected EOF"), std::string_view::npos);
        EXPECT_NE(message.find("read 3 of 8 bytes"), std::string_view::npos);
        EXPECT_NE(message.find(path), std::string_view::npos);
    }
}

TEST_F(FileBufferTest, ReadFailureReportsPathAndSystemError) {
    const auto path = file_path("write-only");
    const auto output =
        std::unique_ptr<std::FILE, decltype(&std::fclose)>{std::fopen(path.c_str(), "wb"), &std::fclose};
    ASSERT_NE(output, nullptr);
    auto destination = std::array<char, 8>{};
    try {
        detail::read_file_contents(::fileno(output.get()), std::span<char>{destination}, path);
        FAIL() << "Expected reading a write-only descriptor to fail";
    } catch (const LogConfig::Exception &error) {
        const auto message = std::string_view{error.what()};
        EXPECT_NE(message.find(path), std::string_view::npos);
        EXPECT_NE(message.find(std::error_code{EBADF, std::generic_category()}.message()), std::string_view::npos);
    }
}

TEST_F(FileBufferTest, SuccessfulLoadClosesDescriptorBeforeReturning) {
    ASSERT_NO_FATAL_FAILURE(write_file("loaded", "content"));
    const auto path = file_path("loaded");
    const auto descriptor_before = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    ASSERT_NE(descriptor_before, -1);
    ASSERT_EQ(::close(descriptor_before), 0);

    const auto file = read_file(path);

    const auto descriptor_after = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    ASSERT_NE(descriptor_after, -1);
    EXPECT_EQ(descriptor_after, descriptor_before);
    EXPECT_EQ(::close(descriptor_after), 0);
    EXPECT_TRUE(file.is_open());
    EXPECT_EQ(file.content(), "content");
}

// ─── Integration test: FileBuffer + lines + split ────────────────────────────

TEST(FileIOIntegrationTest, ParseDictLines) {
    auto path = std::string(DICT_DIR) + "/jieba.dict.utf8";
    auto file = read_file(path);
    auto content = file.content();

    auto line_count = size_t{0};
    auto three_field_count = size_t{0};

    auto lines = get_line_view(content);
    for (auto &&line : lines) {
        if (line.empty()) {
            continue;
        }
        ++line_count;

        auto field_count = size_t{0};
        auto split = get_split_view(line, ' ');
        for ([[maybe_unused]] auto &&field : split) {
            ++field_count;
        }
        if (field_count == 3) {
            ++three_field_count;
        }
    }

    // jieba.dict.utf8 has ~349K lines, all with 3 columns
    EXPECT_GT(line_count, 300000u);
    EXPECT_EQ(line_count, three_field_count);
}

TEST(FileIOIntegrationTest, ParseStopWords) {
    auto path = std::string(DICT_DIR) + "/stop_words.utf8";
    auto file = read_file(path);

    auto count = size_t{0};
    auto lines = get_line_view(file.content());
    for (auto &&line : lines) {
        if (!line.empty()) {
            ++count;
        }
    }
    EXPECT_GT(count, 1000u);
}

TEST(FileIOIntegrationTest, ZeroCopyVerification) {
    // Verify that string_views from lines/split point into the original file buffer
    auto path = std::string(DICT_DIR) + "/jieba.dict.utf8";
    auto file = read_file(path);
    auto content = file.content();

    auto lines = get_line_view(content);
    for (auto &&line : lines) {
        // Each line's data pointer must be within the owned buffer
        ASSERT_GE(line.data(), content.data());
        ASSERT_LE(line.data() + line.size(), content.data() + content.size());

        auto split = get_split_view(line, ' ');
        for (auto &&field : split) {
            // Each field's data pointer must also be within the owned buffer
            ASSERT_GE(field.data(), content.data());
            ASSERT_LE(field.data() + field.size(), content.data() + content.size());
        }
        break; // Just verify the first line is enough
    }
}
