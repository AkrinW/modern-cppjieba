#pragma once

#include "neo/TokenView.hpp"
#include "neo/Traits.hpp"
#include "neo/detail/Logging.hpp"

#include <cstddef>
#include <iterator>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace neo_cppjieba {

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

    TokenIterator() noexcept = default;
    TokenIterator(std::basic_string_view<CharT> source, std::vector<TokenPosition>::const_iterator position) noexcept
        : source_(source), position_(position) {
    }

    [[nodiscard]] auto operator*() const noexcept -> value_type {
        return {position_->source.slice(source_), *position_};
    }

    auto operator++() noexcept -> TokenIterator & {
        ++position_;
        return *this;
    }

    auto operator++(int) noexcept -> TokenIterator {
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

    [[nodiscard]] auto operator[](size_t index) const & noexcept -> value_type {
        assert_check([&] { return index < positions_.size(); }, "Token index exceeds the result size");
        const auto &position = positions_[index];
        return {position.source.slice(source()), position};
    }
    auto operator[](size_t) const && noexcept -> value_type = delete;

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

    BasicTokens(Storage source,
                std::vector<TokenPosition> positions) noexcept(std::is_nothrow_move_constructible_v<Storage>)
        : source_(std::move(source)), positions_(std::move(positions)) {
    }

    Storage source_;
    std::vector<TokenPosition> positions_;
};

} // namespace detail

} // namespace neo_cppjieba
