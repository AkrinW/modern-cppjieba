#pragma once

#include <cstddef>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

extern "C" {

struct CJieba;

struct FfiStr {
    char *data;
    size_t len;
    bool owned;
};

struct CJiebaWords {
    FfiStr *words;
    size_t len;
};

auto jieba_empty() -> CJieba *;
auto jieba_load_dict(CJieba *cjieba, const char *path, size_t len) -> bool;
auto jieba_cut(CJieba *cjieba, const char *sentence, size_t len, bool hmm) -> CJiebaWords *;
auto jieba_cut_all(CJieba *cjieba, const char *sentence, size_t len) -> CJiebaWords *;
auto jieba_cut_for_search(CJieba *cjieba, const char *sentence, size_t len, bool hmm) -> CJiebaWords *;
auto jieba_add_word(CJieba *cjieba, const char *word, size_t len) -> size_t;
auto jieba_words_free(CJiebaWords *words) -> void;
auto jieba_free(CJieba *cjieba) -> void;

} // extern "C"

class RustJieba {
public:
    RustJieba(std::string_view dict_path, std::string_view user_dict_path = "") : handle_(jieba_empty()) {
        if (handle_ == nullptr) {
            throw std::runtime_error("jieba_empty() failed");
        }
        if (!load_dict(dict_path)) {
            jieba_free(handle_);
            handle_ = nullptr;
            throw std::runtime_error("failed to load rust jieba base dictionary");
        }
        if (!user_dict_path.empty() && !load_user_dict(user_dict_path)) {
            jieba_free(handle_);
            handle_ = nullptr;
            throw std::runtime_error("failed to load rust jieba user dictionary");
        }
    }

    RustJieba(const RustJieba &) = delete;
    auto operator=(const RustJieba &) -> RustJieba & = delete;

    RustJieba(RustJieba &&other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    auto operator=(RustJieba &&other) noexcept -> RustJieba & {
        if (this != &other) {
            reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    ~RustJieba() {
        reset();
    }

    [[nodiscard]] auto cut(std::string_view sentence, bool hmm = true) const -> std::vector<std::string> {
        return collect_words(jieba_cut(handle_, sentence.data(), sentence.size(), hmm));
    }

    [[nodiscard]] auto cut_all(std::string_view sentence) const -> std::vector<std::string> {
        return collect_words(jieba_cut_all(handle_, sentence.data(), sentence.size()));
    }

    [[nodiscard]] auto cut_for_search(std::string_view sentence, bool hmm = true) const -> std::vector<std::string> {
        return collect_words(jieba_cut_for_search(handle_, sentence.data(), sentence.size(), hmm));
    }

private:
    [[nodiscard]] static auto collect_words(CJiebaWords *result) -> std::vector<std::string> {
        if (result == nullptr) {
            return {};
        }

        auto words = std::vector<std::string>{};
        words.reserve(result->len);
        for (auto i = size_t{0}; i < result->len; ++i) {
            const auto &word = result->words[i];
            words.emplace_back(word.data, word.len);
        }
        jieba_words_free(result);
        return words;
    }

    [[nodiscard]] auto load_dict(std::string_view path) -> bool {
        return jieba_load_dict(handle_, path.data(), path.size());
    }

    [[nodiscard]] auto load_user_dict(std::string_view path) -> bool {
        if (load_dict(path)) {
            return true;
        }

        auto input = std::ifstream(std::string(path));
        if (!input.is_open()) {
            return false;
        }

        auto line = std::string{};
        auto loaded_any = false;
        while (std::getline(input, line)) {
            auto iss = std::istringstream(line);
            auto word = std::string{};
            if (!(iss >> word)) {
                continue;
            }
            jieba_add_word(handle_, word.data(), word.size());
            loaded_any = true;
        }
        return input.eof() && loaded_any;
    }

    auto reset() noexcept -> void {
        if (handle_ != nullptr) {
            jieba_free(handle_);
            handle_ = nullptr;
        }
    }

    CJieba *handle_ = nullptr;
};
