#include "neo/Jieba.hpp"
#include "neo/TokenView.hpp"
#include "neo/Workspace.hpp"

#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <print>
#include <string>

// Count tokens across UTF-8 input lines without creating a per-line output array.
auto main(int argc, char *argv[]) -> int {
    if (argc != 2) {
        std::println(stderr, "Usage: {} <dict-dir> < input.txt", argv[0]);
        return 2;
    }
    try {
        const auto dict_dir = std::filesystem::path{argv[1]};
        const auto jieba =
            neo_cppjieba::Jieba{(dict_dir / "jieba.dict.utf8").string(), (dict_dir / "hmm_model.utf8").string(), ""};
        auto workspace = neo_cppjieba::Workspace{};
        auto counts = std::map<std::string, std::size_t>{};
        for (auto line = std::string{}; std::getline(std::cin, line);) {
            jieba.cut_each(
                line, neo_cppjieba::CutMode::MIX,
                [&](neo_cppjieba::TokenView<char> token) {
                    // Map keys own their text because the next input line replaces the borrowed source.
                    ++counts[std::string{token.word}];
                },
                workspace);
        }
        if (std::cin.bad()) {
            std::println(stderr, "Failed to read UTF-8 input from stdin.");
            return 1;
        }
        for (const auto &[word, count] : counts) {
            std::println("{}\t{}", count, word);
        }
        return 0;
    } catch (const std::exception &error) {
        std::println(stderr, "{}", error.what());
        return 1;
    }
}
