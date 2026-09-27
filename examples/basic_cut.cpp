#include "neo/Jieba.hpp"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <print>
#include <string_view>

// Keep the source alive while printing the borrowed tokens for one mode.
auto print_cut(const neo_cppjieba::Jieba &jieba, std::string_view text, neo_cppjieba::CutMode mode,
               std::string_view name) -> void {
    std::print("{}:", name);
    for (const auto token : jieba.cut(text, mode)) {
        std::print(" [{}]", token.word);
    }
    std::println("");
}

// Compare all public segmentation modes for the same UTF-8 text.
auto main(int argc, char *argv[]) -> int {
    if (argc != 3) {
        std::println(stderr, "Usage: {} <dict-dir> <utf8-text>", argv[0]);
        return 2;
    }
    try {
        const auto dict_dir = std::filesystem::path{argv[1]};
        const auto jieba =
            neo_cppjieba::Jieba{(dict_dir / "jieba.dict.utf8").string(), (dict_dir / "hmm_model.utf8").string(), ""};
        const auto text = std::string_view{argv[2]};
        using neo_cppjieba::CutMode;
        print_cut(jieba, text, CutMode::MIX, "MIX");
        print_cut(jieba, text, CutMode::MIX_NO_HMM, "MIX_NO_HMM");
        print_cut(jieba, text, CutMode::MP, "MP");
        print_cut(jieba, text, CutMode::HMM, "HMM");
        print_cut(jieba, text, CutMode::FULL, "FULL");
        print_cut(jieba, text, CutMode::SEARCH, "SEARCH");
        print_cut(jieba, text, CutMode::SEARCH_NO_HMM, "SEARCH_NO_HMM");
        return 0;
    } catch (const std::exception &error) {
        std::println(stderr, "{}", error.what());
        return 1;
    }
}
