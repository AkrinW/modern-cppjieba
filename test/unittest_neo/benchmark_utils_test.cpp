#include "../../benchmark/BenchmarkUtils.hpp"
#include "../TestUtils.hpp"
#include "gtest/gtest.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

// Own fixture files for the portable benchmark input loader.
class BenchmarkInputTest : public ::testing::Test {
protected:
    void SetUp() override {
        directory_ = create_temp_directory("neo-benchmark-input-");
    }

    void TearDown() override {
        if (!directory_.empty()) {
            auto error = std::error_code{};
            std::filesystem::remove_all(directory_, error);
            EXPECT_FALSE(error) << error.message();
        }
    }

    void write_file(const std::filesystem::path &path, std::string_view contents) const {
        auto output = std::ofstream{path, std::ios::binary};
        ASSERT_TRUE(output.is_open());
        output << contents;
        output.close();
        ASSERT_TRUE(output.good());
    }

    std::filesystem::path directory_;
};

TEST_F(BenchmarkInputTest, HandlesCrLfEmptyLinesAndMissingFinalNewline) {
    const auto path = directory_ / "lines";
    ASSERT_NO_FATAL_FAILURE(write_file(path, "first\r\n\r\n\n中文\nlast"));
    EXPECT_EQ(load_lines(path_to_utf8(path)), (std::vector<std::string>{"first", "中文", "last"}));
}

TEST_F(BenchmarkInputTest, PreservesBinaryBytesInLines) {
    const auto path = directory_ / "binary";
    const auto line = std::string{"a\0b\x1az", 5};
    ASSERT_NO_FATAL_FAILURE(write_file(path, line + '\n'));
    EXPECT_EQ(load_lines(path_to_utf8(path)), (std::vector<std::string>{line}));
}

TEST_F(BenchmarkInputTest, ReadsUtf8Path) {
    const auto path = directory_ / std::filesystem::path{u8"输入𠮷.txt"};
    ASSERT_NO_FATAL_FAILURE(write_file(path, "中文\n"));
    EXPECT_EQ(load_lines(path_to_utf8(path)), (std::vector<std::string>{"中文"}));
}

TEST_F(BenchmarkInputTest, EmptyAndMissingFilesProduceNoLines) {
    const auto path = directory_ / "empty";
    ASSERT_NO_FATAL_FAILURE(write_file(path, ""));
    EXPECT_TRUE(load_lines(path_to_utf8(path)).empty());
    EXPECT_TRUE(load_lines(path_to_utf8(directory_ / "missing")).empty());
}

} // namespace
