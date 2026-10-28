#pragma once

#include "neo/Fwd.hpp"
#include "neo/concept/Text.hpp"
#include "neo/util/FileIO.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace neo_cppjieba {

namespace detail {
template <SourceTextParser Parse>
auto with_source_text(const DictSource &source, Parse &&parse) -> decltype(auto);
} // namespace detail

// One dictionary, model, IDF or stop-word input: a file read at load time, or UTF-8 text already in memory.
// Memory sources serve embedded resources and platforms without a usable file system, such as WASM.
// The file path is copied. Memory text is borrowed and must stay valid until the loader that reads it returns;
// loaded objects keep no reference to it. The name only labels diagnostics.
class DictSource {
public:
    [[nodiscard]] static auto file(std::string_view path) -> DictSource {
        return DictSource{true, std::string{path}, {}};
    }

    [[nodiscard]] static auto memory(std::string_view text, std::string_view name = "<memory>") -> DictSource {
        return DictSource{false, std::string{name}, text};
    }

    [[nodiscard]] auto is_file() const noexcept -> bool {
        return is_file_;
    }

    // The file path, or the diagnostic name of a memory source.
    [[nodiscard]] auto name() const noexcept -> std::string_view {
        return name_;
    }

private:
    DictSource(bool is_file, std::string name, std::string_view text)
        : is_file_(is_file), name_(std::move(name)), text_(text) {
    }

    template <detail::SourceTextParser Parse>
    friend auto detail::with_source_text(const DictSource &source, Parse &&parse) -> decltype(auto);

    bool is_file_;
    std::string name_;
    std::string_view text_;
};

namespace detail {

// Loaders call parse(text, name). File contents live only for the duration of the call.
template <SourceTextParser Parse>
auto with_source_text(const DictSource &source, Parse &&parse) -> decltype(auto) {
    if (source.is_file_) {
        const auto file = read_file(source.name_);
        return std::forward<Parse>(parse)(file.content(), source.name());
    }
    return std::forward<Parse>(parse)(source.text_, source.name());
}

} // namespace detail

} // namespace neo_cppjieba
