#include "neo/Jieba.hpp"
#include "neo/Workspace.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <print>
#include <string>

// Tokenize a UTF-8 stream while reusing one model and one caller-owned workspace.
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
        for (auto line = std::string{}; std::getline(std::cin, line);) {
            // Consume borrowed words before getline replaces the source text.
            const auto tokens = jieba.cut_with_workspace(line, neo_cppjieba::CutMode::MIX, workspace);
            for (const auto token : tokens) {
                std::print("[{}]", token.word);
            }
            std::println("");
        }
        if (std::cin.bad()) {
            std::println(stderr, "Failed to read UTF-8 input from stdin.");
            return 1;
        }
        return 0;
    } catch (const std::exception &error) {
        std::println(stderr, "{}", error.what());
        return 1;
    }
}
