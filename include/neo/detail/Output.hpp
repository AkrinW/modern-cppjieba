#pragma once

#include "neo/TokenView.hpp"
#include "neo/Traits.hpp"
#include "neo/Unicode.hpp"
#include "neo/detail/Logging.hpp"

#include <concepts>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace neo_cppjieba::detail {

// Reusable output contains source ranges, complete positions, or words in the source encoding.
template <typename Output, typename CharT>
concept OutputValue = CharType<CharT>
                      && (std::same_as<Output, SourceRange> || std::same_as<Output, TokenPosition>
                          || std::same_as<Output, std::basic_string<CharT>>);

// Invoke visitors as lvalues without copying them; failure is reported through exceptions.
template <typename Emit, typename CharT>
concept TokenVisitor = CharType<CharT> && std::invocable<Emit &, TokenView<CharT>>
                       && std::same_as<std::invoke_result_t<Emit &, TokenView<CharT>>, void>;

// The offset sentinel maps the exclusive rune end to the corresponding source boundary.
[[nodiscard]] inline auto to_source_range(WordRange range, std::span<const uint32_t> offsets) -> SourceRange {
    assert_check([&] { return range.begin <= range.end && range.end < offsets.size(); },
                 "Jieba rune range exceeds its source offset table");
    return {offsets[range.begin], offsets[range.end]};
}

// Fill the caller's cleared output from validated ranges, retaining the output capacity.
template <CharType CharT, OutputValue<CharT> Output>
inline auto fill_output(std::basic_string_view<CharT> source, std::span<const WordRange> ranges,
                        std::span<const uint32_t> offsets, std::vector<Output> &out) -> void {
    out.reserve(ranges.size());
    for (const auto &range : ranges) {
        const auto source_range = to_source_range(range, offsets);
        if constexpr (std::same_as<Output, SourceRange>) {
            out.push_back(source_range);
        } else if constexpr (std::same_as<Output, TokenPosition>) {
            out.push_back({range, source_range});
        } else {
            out.emplace_back(source_range.slice(source));
        }
    }
}

// Deliver borrowed views in segmentation order; visitor exceptions propagate to the caller.
template <CharType CharT, TokenVisitor<CharT> Emit>
inline auto emit_tokens(std::basic_string_view<CharT> source, std::span<const WordRange> ranges,
                        std::span<const uint32_t> offsets, Emit &emit) -> void {
    for (const auto &range : ranges) {
        const auto source_range = to_source_range(range, offsets);
        std::invoke(emit, TokenView<CharT>{source_range.slice(source), {range, source_range}});
    }
}

} // namespace neo_cppjieba::detail
