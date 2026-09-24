#pragma once

#include "neo/Traits.hpp"
#include "neo/Unicode.hpp"
#include "neo/detail/Logging.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace neo_cppjieba {

// A half-open range of original code units: bytes for UTF-8, native units for other encodings.
struct SourceRange {
    uint32_t begin;
    uint32_t end;

    [[nodiscard]] constexpr auto size() const noexcept -> uint32_t {
        assert_check([&] { return begin <= end; }, "Reversed internal SourceRange [{}, {})", begin, end);
        return end - begin;
    }

    template <CharType CharT>
    [[nodiscard]] constexpr auto slice(std::basic_string_view<CharT> source) const -> std::basic_string_view<CharT> {
        assert_check([&] { return begin <= end && end <= source.size(); }, "SourceRange exceeds its input");
        return source.substr(begin, end - begin);
    }

    constexpr auto operator==(const SourceRange &) const noexcept -> bool = default;
};

// Compact token metadata keeps rune and source-code-unit coordinates distinct.
struct TokenPosition {
    WordRange runes;
    SourceRange source;

    constexpr auto operator==(const TokenPosition &) const noexcept -> bool = default;
};

// The word borrows the original text; position metadata is copied into the view.
template <CharType CharT>
struct TokenView {
    std::basic_string_view<CharT> word;
    TokenPosition position;
};

class Jieba;

namespace detail {

// Iteration materializes views on demand instead of storing a text pointer in every token.
template <CharType CharT>
class TokenIterator {
public:
    using iterator_concept = std::forward_iterator_tag;
    using iterator_category = std::forward_iterator_tag;
    using value_type = TokenView<CharT>;
    using difference_type = std::ptrdiff_t;
    using reference = value_type;

    TokenIterator() = default;
    TokenIterator(std::basic_string_view<CharT> source, std::vector<TokenPosition>::const_iterator position)
        : source_(source), position_(position) {
    }

    [[nodiscard]] auto operator*() const -> value_type {
        return {position_->source.slice(source_), *position_};
    }

    auto operator++() -> TokenIterator & {
        ++position_;
        return *this;
    }

    auto operator++(int) -> TokenIterator {
        auto previous = *this;
        ++*this;
        return previous;
    }

    [[nodiscard]] auto operator==(const TokenIterator &other) const noexcept -> bool {
        return position_ == other.position_;
    }

private:
    std::basic_string_view<CharT> source_{};
    std::vector<TokenPosition>::const_iterator position_{};
};

// Owns positions and stores either borrowed or owned source text; no cached pointers into owned strings.
template <typename Storage>
class BasicTokens {
public:
    using char_type = typename Storage::value_type;
    using value_type = TokenView<char_type>;
    using const_iterator = TokenIterator<char_type>;

    [[nodiscard]] auto size() const noexcept -> size_t {
        return positions_.size();
    }

    [[nodiscard]] auto empty() const noexcept -> bool {
        return positions_.empty();
    }

    [[nodiscard]] auto source() const & noexcept -> std::basic_string_view<char_type> {
        return source_;
    }
    auto source() const && noexcept -> std::basic_string_view<char_type> = delete;

    [[nodiscard]] auto positions() const & noexcept -> std::span<const TokenPosition> {
        return positions_;
    }
    auto positions() const && noexcept -> std::span<const TokenPosition> = delete;

    [[nodiscard]] auto operator[](size_t index) const & -> value_type {
        assert_check([&] { return index < positions_.size(); }, "Token index exceeds the result size");
        const auto &position = positions_[index];
        return {position.source.slice(source()), position};
    }
    auto operator[](size_t) const && -> value_type = delete;

    [[nodiscard]] auto begin() const & noexcept -> const_iterator {
        return {source(), positions_.cbegin()};
    }
    auto begin() const && noexcept -> const_iterator = delete;

    [[nodiscard]] auto end() const & noexcept -> const_iterator {
        return {source(), positions_.cend()};
    }
    auto end() const && noexcept -> const_iterator = delete;

private:
    friend class ::neo_cppjieba::Jieba;

    BasicTokens(Storage source, std::vector<TokenPosition> positions)
        : source_(std::move(source)), positions_(std::move(positions)) {
    }

    Storage source_;
    std::vector<TokenPosition> positions_;
};

} // namespace detail

template <CharType CharT>
using Tokens = detail::BasicTokens<std::basic_string_view<CharT>>;

template <CharType CharT>
using OwnedTokens = detail::BasicTokens<std::basic_string<CharT>>;

} // namespace neo_cppjieba
