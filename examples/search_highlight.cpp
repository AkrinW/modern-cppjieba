#include "neo/Jieba.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <print>
#include <string_view>

// Use SEARCH token byte offsets to highlight an exact token in the original UTF-8 text.
auto main(int argc, char *argv[]) -> int {
    if (argc != 4) {
        std::println(stderr, "Usage: {} <dict-dir> <utf8-text> <query-token>", argv[0]);
        return 2;
    }
    try {
        const auto dict_dir = std::filesystem::path{argv[1]};
        const auto jieba =
            neo_cppjieba::Jieba{(dict_dir / "jieba.dict.utf8").string(), (dict_dir / "hmm_model.utf8").string(), ""};
        const auto text = std::string_view{argv[2]};
        const auto query = std::string_view{argv[3]};
        auto found = false;
        for (const auto token : jieba.cut(text, neo_cppjieba::CutMode::SEARCH)) {
            if (token.word != query) {
                continue;
            }
            found = true;
            const auto source = token.position.source;
            const auto runes = token.position.runes;
            std::println("bytes=[{}, {}) runes=[{}, {})", source.begin, source.end, runes.begin, runes.end);
            std::println("{}[{}]{}", text.substr(0, source.begin), token.word, text.substr(source.end));
        }
        if (!found) {
            std::println("No token matches: {}", query);
        }
        return 0;
    } catch (const std::exception &error) {
        std::println(stderr, "{}", error.what());
        return 1;
    }
}
