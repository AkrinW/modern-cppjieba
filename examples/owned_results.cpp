#include "neo/Jieba.hpp"
#include "neo/Token.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <print>
#include <string>
#include <utility>

// Transfer a request's text into a result that can outlive this function.
auto tokenize_message(const neo_cppjieba::Jieba &jieba, std::string message) -> neo_cppjieba::OwnedTokens<char> {
    return jieba.cut_owned(std::move(message), neo_cppjieba::CutMode::MIX);
}

// Show the two storage choices for tokens that must outlive the caller's input.
auto main(int argc, char *argv[]) -> int {
    if (argc != 3) {
        std::println(stderr, "Usage: {} <dict-dir> <utf8-text>", argv[0]);
        return 2;
    }
    try {
        const auto dict_dir = std::filesystem::path{argv[1]};
        const auto jieba =
            neo_cppjieba::Jieba{(dict_dir / "jieba.dict.utf8").string(), (dict_dir / "hmm_model.utf8").string(), ""};
        const auto result = tokenize_message(jieba, std::string{argv[2]});
        std::println("Owned source: {}", result.source());
        for (const auto token : result) {
            std::println("[{}] bytes=[{}, {})", token.word, token.position.source.begin, token.position.source.end);
        }

        // Choose independent strings when the original sentence and token offsets are unnecessary.
        const auto words = jieba.cut_strings(result.source(), neo_cppjieba::CutMode::MIX);
        std::println("Independent strings:");
        for (const auto &word : words) {
            std::println("[{}]", word);
        }
        return 0;
    } catch (const std::exception &error) {
        std::println(stderr, "{}", error.what());
        return 1;
    }
}
