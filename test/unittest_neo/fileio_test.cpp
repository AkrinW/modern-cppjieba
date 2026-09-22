#include "gtest/gtest.h"
#include "neo/FileIO.hpp"
#include "neo/StringUtil.hpp"

#include "test_paths.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using namespace neo_cppjieba;

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

// ─── MappedFile tests ────────────────────────────────────────────────────────

TEST(MappedFileTest, OpenDictFile) {
    auto path = std::string(DICT_DIR) + "/jieba.dict.utf8";
    auto file = get_map_file(path);
    EXPECT_TRUE(file.is_open());
    EXPECT_GT(file.size(), 0u);
    // The dict file should start with a known entry
    auto content = file.content();
    EXPECT_FALSE(content.empty());
}

TEST(MappedFileTest, MoveSemantics) {
    auto path = std::string(DICT_DIR) + "/stop_words.utf8";
    auto file1 = get_map_file(path);
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

// ─── Integration test: MappedFile + lines + split ────────────────────────────

TEST(FileIOIntegrationTest, ParseDictLines) {
    auto path = std::string(DICT_DIR) + "/jieba.dict.utf8";
    auto file = get_map_file(path);
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
    auto file = get_map_file(path);

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
    // Verify that string_views from lines/split point into the original mapped data
    auto path = std::string(DICT_DIR) + "/jieba.dict.utf8";
    auto file = get_map_file(path);
    auto content = file.content();

    auto lines = get_line_view(content);
    for (auto &&line : lines) {
        // Each line's data pointer must be within the mapped region
        ASSERT_GE(line.data(), content.data());
        ASSERT_LE(line.data() + line.size(), content.data() + content.size());

        auto split = get_split_view(line, ' ');
        for (auto &&field : split) {
            // Each field's data pointer must also be within the mapped region
            ASSERT_GE(field.data(), content.data());
            ASSERT_LE(field.data() + field.size(), content.data() + content.size());
        }
        break; // Just verify the first line is enough
    }
}
