#include "neo/DictTrie.hpp"
#include "neo/FullSegment.hpp"
#include "neo/HMMSegment.hpp"
#include "neo/HMModel.hpp"
#include "neo/Jieba.hpp"
#include "neo/MPSegment.hpp"
#include "neo/MixSegment.hpp"
#include "neo/QuerySegment.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using namespace neo_cppjieba;

template <typename Input, typename = void>
inline constexpr bool full_segment_invocable_v = false;
template <typename Input>
inline constexpr bool full_segment_invocable_v<
    Input, std::void_t<decltype(FullSegment::cut(std::declval<const DictTrie &>(), std::declval<Input>()))>> = true;

template <typename Input, typename = void>
inline constexpr bool mp_segment_invocable_v = false;
template <typename Input>
inline constexpr bool mp_segment_invocable_v<
    Input, std::void_t<decltype(MPSegment::cut(std::declval<const DictTrie &>(), std::declval<Input>()))>> = true;

template <typename Input, typename = void>
inline constexpr bool hmm_segment_invocable_v = false;
template <typename Input>
inline constexpr bool hmm_segment_invocable_v<
    Input, std::void_t<decltype(HMMSegment::cut(std::declval<const HMModel &>(), std::declval<Input>()))>> = true;

template <typename Input, typename = void>
inline constexpr bool mix_segment_invocable_v = false;
template <typename Input>
inline constexpr bool mix_segment_invocable_v<
    Input, std::void_t<decltype(MixSegment<>::cut(std::declval<const DictTrie &>(), std::declval<const HMModel &>(),
                                                  std::declval<Input>()))>> = true;

template <typename Input, typename = void>
inline constexpr bool query_segment_invocable_v = false;
template <typename Input>
inline constexpr bool query_segment_invocable_v<
    Input, std::void_t<decltype(QuerySegment<>::cut(std::declval<const DictTrie &>(), std::declval<const HMModel &>(),
                                                    std::declval<Input>()))>> = true;

template <typename Input, typename = void>
inline constexpr bool jieba_cut_invocable_v = false;
template <typename Input>
inline constexpr bool jieba_cut_invocable_v<
    Input, std::void_t<decltype(std::declval<const Jieba &>().cut(std::declval<Input>(), CutMode::MIX))>> = true;

// Mode validation must participate in overload selection for every public input family.
// Modes now validate at runtime; borrowing constraints are checked during overload selection.
template <typename Input>
concept JiebaImplicitModeInvocable = requires { std::declval<const Jieba &>().cut(std::declval<Input>()); };

// Result views require a live result object, including when the result owns its source.
template <typename Result>
concept TokenViewsAccessible = requires {
    std::declval<Result>().source();
    std::declval<Result>().positions();
    std::declval<Result>()[0];
    std::declval<Result>().begin();
};

// Borrowed resources must remain accessible through lvalues, including const owners.
template <typename Owner>
concept JiebaDictAccessible = requires {
    { std::declval<Owner>().dict() } -> std::same_as<const DictTrie &>;
};

template <typename Owner>
concept JiebaModelAccessible = requires {
    { std::declval<Owner>().model() } -> std::same_as<const HMModel &>;
};

static_assert(!std::default_initializable<Jieba>);
static_assert(!std::constructible_from<Jieba, std::string_view, std::string_view>);
static_assert(std::constructible_from<Jieba, std::string_view, std::string_view, std::string_view>);

static_assert(JiebaDictAccessible<Jieba &>);
static_assert(JiebaDictAccessible<const Jieba &>);
static_assert(!JiebaDictAccessible<Jieba &&>);
static_assert(!JiebaDictAccessible<const Jieba &&>);
static_assert(JiebaModelAccessible<Jieba &>);
static_assert(JiebaModelAccessible<const Jieba &>);
static_assert(!JiebaModelAccessible<Jieba &&>);
static_assert(!JiebaModelAccessible<const Jieba &&>);

static_assert(!JiebaImplicitModeInvocable<std::string_view>);
static_assert(jieba_cut_invocable_v<std::string &>);
static_assert(jieba_cut_invocable_v<const std::string &>);
static_assert(!jieba_cut_invocable_v<std::string>);
static_assert(!jieba_cut_invocable_v<const std::string>);
static_assert(jieba_cut_invocable_v<std::vector<std::byte> &>);
static_assert(!jieba_cut_invocable_v<std::vector<std::byte>>);
static_assert(std::ranges::forward_range<Tokens<char>>);
static_assert(std::ranges::sized_range<Tokens<char>>);
static_assert(std::ranges::forward_range<OwnedTokens<char>>);
static_assert(TokenViewsAccessible<Tokens<char> &>);
static_assert(TokenViewsAccessible<const Tokens<char> &>);
static_assert(!TokenViewsAccessible<Tokens<char> &&>);
static_assert(!TokenViewsAccessible<const Tokens<char> &&>);
static_assert(TokenViewsAccessible<OwnedTokens<char> &>);
static_assert(TokenViewsAccessible<const OwnedTokens<char> &>);
static_assert(!TokenViewsAccessible<OwnedTokens<char> &&>);
static_assert(!TokenViewsAccessible<const OwnedTokens<char> &&>);

static_assert(full_segment_invocable_v<std::span<const Rune>>);
static_assert(!full_segment_invocable_v<std::string_view>);
static_assert(
    std::same_as<decltype(FullSegment::cut(std::declval<const DictTrie &>(), std::declval<std::span<const Rune>>())),
                 std::vector<WordRange>>);

static_assert(mp_segment_invocable_v<std::span<const Rune>>);
static_assert(!mp_segment_invocable_v<std::string_view>);
static_assert(
    std::same_as<decltype(MPSegment::cut(std::declval<const DictTrie &>(), std::declval<std::span<const Rune>>())),
                 std::vector<WordRange>>);

static_assert(hmm_segment_invocable_v<std::span<const Rune>>);
static_assert(!hmm_segment_invocable_v<std::string_view>);
static_assert(
    std::same_as<decltype(HMMSegment::cut(std::declval<const HMModel &>(), std::declval<std::span<const Rune>>())),
                 std::vector<WordRange>>);

static_assert(mix_segment_invocable_v<std::span<const Rune>>);
static_assert(!mix_segment_invocable_v<std::string_view>);
static_assert(std::same_as<decltype(MixSegment<>::cut(std::declval<const DictTrie &>(), std::declval<const HMModel &>(),
                                                      std::declval<std::span<const Rune>>())),
                           std::vector<WordRange>>);

static_assert(query_segment_invocable_v<std::span<const Rune>>);
static_assert(!query_segment_invocable_v<std::string_view>);
static_assert(
    std::same_as<decltype(QuerySegment<>::cut(std::declval<const DictTrie &>(), std::declval<const HMModel &>(),
                                              std::declval<std::span<const Rune>>())),
                 std::vector<WordRange>>);

static_assert(jieba_cut_invocable_v<std::span<const Rune>>);
static_assert(jieba_cut_invocable_v<std::string_view>);
static_assert(std::same_as<decltype(std::declval<const Jieba &>().cut(std::declval<std::string_view>(), CutMode::MIX)),
                           Tokens<char>>);
static_assert(
    std::same_as<decltype(std::declval<const Jieba &>().cut(std::declval<std::u32string_view>(), CutMode::MIX)),
                 Tokens<char32_t>>);
static_assert(
    std::same_as<decltype(std::declval<const Jieba &>().cut(std::declval<std::span<const Rune>>(), CutMode::MIX)),
                 Tokens<char32_t>>);
static_assert(
    std::same_as<decltype(std::declval<const Jieba &>().cut_runes(std::declval<std::span<const Rune>>(), CutMode::MIX)),
                 std::vector<WordRange>>);
static_assert(
    std::same_as<decltype(std::declval<const Jieba &>().cut_runes(std::declval<std::span<Rune, 5>>(), CutMode::MIX)),
                 std::vector<WordRange>>);
static_assert(std::same_as<decltype(std::declval<const Jieba &>().cut_runes(std::declval<std::span<const Rune, 0>>(),
                                                                            CutMode::MIX)),
                           std::vector<WordRange>>);

// Rune output has no decoding state and therefore needs no decoding-buffer argument.
static_assert(requires(const Jieba &jieba, std::span<const Rune> runes, std::vector<WordRange> &out) {
    jieba.cut_runes_into(runes, CutMode::MIX, out);
});
