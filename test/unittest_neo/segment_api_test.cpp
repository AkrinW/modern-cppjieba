#include "neo/Jieba.hpp"
#include "neo/Token.hpp"
#include "neo/TokenView.hpp"
#include "neo/Unicode.hpp"
#include "neo/Workspace.hpp"
#include "neo/detail/Dag.hpp"
#include "neo/detail/DictTrie.hpp"
#include "neo/detail/FullSegment.hpp"
#include "neo/detail/HMMSegment.hpp"
#include "neo/detail/HMModel.hpp"
#include "neo/detail/MPSegment.hpp"
#include "neo/detail/MixSegment.hpp"
#include "neo/detail/Output.hpp"
#include "neo/detail/QuerySegment.hpp"
#include "neo/detail/StringUtil.hpp"
#include "neo/detail/Trie.hpp"
#include "neo/detail/Unicode.hpp"

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

// Explicit workspace borrowing keeps the same source lifetime and result type as cut.
template <typename Input, typename Buffer>
concept JiebaWorkspaceCutInvocable = requires(const Jieba &jieba) {
    {
        jieba.cut_with_workspace(std::declval<Input>(), CutMode::MIX, std::declval<Buffer>())
    } -> std::same_as<Tokens<output_char_type_t<Input>>>;
};

// Reusable output accepts documented value types and preserves the input's character encoding.
template <typename Input, typename Output>
concept JiebaCutIntoInvocable =
    requires(const Jieba &jieba, const Input &input, std::vector<Output> &out, Workspace &workspace) {
        { jieba.cut_into(input, CutMode::MIX, out, workspace) } -> std::same_as<void>;
    };

// Visitors receive the matching token character type and return void.
template <typename Input, typename Emit>
concept JiebaCutEachInvocable = requires(const Jieba &jieba, const Input &input, Emit &&emit, Workspace &workspace) {
    { jieba.cut_each(input, CutMode::MIX, std::forward<Emit>(emit), workspace) } -> std::same_as<void>;
};

// Result views require a live result object, including when the result owns its source.
// Borrowed resources must remain accessible through lvalues, including const owners.
template <typename Result>
concept TokenViewsAccessible = requires {
    std::declval<Result>().source();
    std::declval<Result>().positions();
    std::declval<Result>()[0];
    std::declval<Result>().begin();
};

// Dictionary and model storage stay private for every Jieba value category.
template <typename Owner>
concept JiebaDictAccessible = requires { std::declval<Owner>().dict(); };

template <typename Owner>
concept JiebaModelAccessible = requires { std::declval<Owner>().model(); };

static_assert(!std::default_initializable<Jieba>);
static_assert(!std::constructible_from<Jieba, std::string_view, std::string_view>);
static_assert(std::constructible_from<Jieba, std::string_view, std::string_view, std::string_view>);

static_assert(!JiebaDictAccessible<Jieba &>);
static_assert(!JiebaDictAccessible<const Jieba &>);
static_assert(!JiebaDictAccessible<Jieba &&>);
static_assert(!JiebaDictAccessible<const Jieba &&>);
static_assert(!JiebaModelAccessible<Jieba &>);
static_assert(!JiebaModelAccessible<const Jieba &>);
static_assert(!JiebaModelAccessible<Jieba &&>);
static_assert(!JiebaModelAccessible<const Jieba &&>);

static_assert(!JiebaImplicitModeInvocable<std::string_view>);
static_assert(jieba_cut_invocable_v<std::string &>);
static_assert(jieba_cut_invocable_v<const std::string &>);
static_assert(!jieba_cut_invocable_v<std::string>);
static_assert(!jieba_cut_invocable_v<const std::string>);
static_assert(jieba_cut_invocable_v<std::vector<std::byte> &>);
static_assert(!jieba_cut_invocable_v<std::vector<std::byte>>);
static_assert(JiebaWorkspaceCutInvocable<std::string &, Workspace &>);
static_assert(JiebaWorkspaceCutInvocable<const std::string &, Workspace &>);
static_assert(JiebaWorkspaceCutInvocable<std::string_view, Workspace &>);
static_assert(JiebaWorkspaceCutInvocable<std::u16string_view, Workspace &>);
static_assert(JiebaWorkspaceCutInvocable<std::span<const Rune>, Workspace &>);
static_assert(JiebaWorkspaceCutInvocable<std::vector<std::byte> &, Workspace &>);
static_assert(JiebaWorkspaceCutInvocable<std::span<const std::byte>, Workspace &>);
static_assert(!JiebaWorkspaceCutInvocable<std::string, Workspace &>);
static_assert(!JiebaWorkspaceCutInvocable<const std::string, Workspace &>);
static_assert(!JiebaWorkspaceCutInvocable<std::vector<std::byte>, Workspace &>);
static_assert(!JiebaWorkspaceCutInvocable<std::string_view, const Workspace &>);
static_assert(!JiebaWorkspaceCutInvocable<std::string_view, Workspace>);
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

static_assert(JiebaCutIntoInvocable<std::string_view, SourceRange>);
static_assert(JiebaCutIntoInvocable<std::string_view, TokenPosition>);
static_assert(JiebaCutIntoInvocable<std::string_view, std::string>);
static_assert(JiebaCutIntoInvocable<std::u16string_view, std::u16string>);
static_assert(JiebaCutIntoInvocable<std::span<const std::byte>, std::string>);
static_assert(!JiebaCutIntoInvocable<std::string_view, WordRange>);
static_assert(!JiebaCutIntoInvocable<std::string_view, TokenView<char>>);
static_assert(!JiebaCutIntoInvocable<std::string_view, std::string_view>);
static_assert(!JiebaCutIntoInvocable<std::string_view, std::u16string>);
static_assert(!JiebaCutIntoInvocable<std::u16string_view, std::string>);

static_assert(JiebaCutEachInvocable<std::string_view, void (*)(TokenView<char>)>);
static_assert(JiebaCutEachInvocable<std::u16string_view, void (*)(TokenView<char16_t>)>);
static_assert(JiebaCutEachInvocable<std::span<const std::byte>, void (*)(TokenView<char>)>);
static_assert(!JiebaCutEachInvocable<std::string_view, int (*)(TokenView<char>)>);
static_assert(!JiebaCutEachInvocable<std::u16string_view, void (*)(TokenView<char>)>);

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
// The explicit workspace now retains algorithm buffers; its decoding storage remains unused here.
static_assert(requires(const Jieba &jieba, std::span<const Rune> runes, std::vector<WordRange> &out,
                       Workspace &workspace) { jieba.cut_runes_into(runes, CutMode::MIX, out, workspace); });
static_assert(std::default_initializable<Workspace>);
static_assert(std::movable<Workspace>);
static_assert(!std::copy_constructible<Workspace>);

// Computation on validated, borrowed storage must not introduce an exception boundary.
static_assert(noexcept(detail::decode_step(std::declval<std::span<const char>>())));
static_assert(noexcept(detail::decode_step(std::declval<std::span<const char16_t>>())));
static_assert(noexcept(detail::decode_step(std::declval<std::span<const char32_t>>())));
static_assert(noexcept(detail::encode_step<char>(U'中')));
static_assert(noexcept(detail::encode_step<char16_t>(U'中')));
static_assert(noexcept(detail::encode_step<char32_t>(U'中')));
static_assert(noexcept(detail::offset_count_for_source(0)));
static_assert(noexcept(detail::to_source_range(std::declval<WordRange>(),
                                               std::declval<std::span<const SourceOffset>>())));
static_assert(noexcept(std::declval<WordRange>().slice(std::declval<std::span<const Rune>>())));
static_assert(noexcept(std::declval<SourceRange>().slice(std::declval<std::string_view>())));
static_assert(noexcept(std::declval<const Tokens<char> &>()[0]));
static_assert(noexcept(std::declval<const OwnedTokens<char> &>()[0]));
static_assert(noexcept(*std::declval<Tokens<char>::const_iterator>()));
static_assert(noexcept(++std::declval<Tokens<char>::const_iterator &>()));
static_assert(noexcept(std::declval<Tokens<char>::const_iterator &>()++));
static_assert(noexcept(get_line_view(std::declval<std::string_view>()).begin()));
static_assert(noexcept(++std::declval<lines_view::iterator &>()));
static_assert(noexcept(get_split_view(std::declval<std::string_view>(), ' ').begin()));
static_assert(noexcept(++std::declval<split_view::iterator &>()));
static_assert(noexcept(std::declval<const Dag &>().get_edges(0)));
static_assert(noexcept(std::declval<const Trie &>().find(std::declval<std::span<const Rune>>())));
static_assert(noexcept(std::declval<const DictTrie &>().find(std::declval<std::span<const Rune>>())));
static_assert(noexcept(std::declval<const HMModel &>().get_start_prob(HMMState::B)));
static_assert(noexcept(std::declval<const HMModel &>().get_trans_prob(HMMState::B, HMMState::E)));

// Generic visitors determine whether allocation-free traversal can promise not to throw.
using MatchVisitor = void (*)(RuneIndex, const DictUnit &);
using NoThrowMatchVisitor = void (*)(RuneIndex, const DictUnit &) noexcept;
using WordVisitor = void (*)(TokenView<char>);
using NoThrowWordVisitor = void (*)(TokenView<char>) noexcept;
static_assert(noexcept(std::declval<const Trie &>().for_each_match_from(std::declval<std::span<const Rune>>(), 0,
                                                                        std::declval<NoThrowMatchVisitor>())));
static_assert(!noexcept(std::declval<const Trie &>().for_each_match_from(std::declval<std::span<const Rune>>(), 0,
                                                                         std::declval<MatchVisitor>())));
static_assert(noexcept(std::declval<const DictTrie &>().for_each_match_from(std::declval<std::span<const Rune>>(), 0,
                                                                            std::declval<NoThrowMatchVisitor>())));
static_assert(!noexcept(std::declval<const DictTrie &>().for_each_match_from(std::declval<std::span<const Rune>>(), 0,
                                                                             std::declval<MatchVisitor>())));
static_assert(noexcept(detail::emit_tokens(std::declval<std::string_view>(), std::declval<std::span<const WordRange>>(),
                                           std::declval<std::span<const SourceOffset>>(),
                                           std::declval<NoThrowWordVisitor &>())));
static_assert(
    !noexcept(detail::emit_tokens(std::declval<std::string_view>(), std::declval<std::span<const WordRange>>(),
                                  std::declval<std::span<const SourceOffset>>(), std::declval<WordVisitor &>())));

// External validation and allocation failures remain recoverable through the existing exception paths.
static_assert(!noexcept(decode(std::declval<std::string_view>())));
static_assert(!noexcept(encode<char>(std::declval<std::span<const Rune>>())));
static_assert(!noexcept(std::declval<const Trie &>().find(std::declval<std::string_view>())));
static_assert(!noexcept(std::declval<const Jieba &>().cut(std::declval<std::string_view>(), CutMode::MIX)));
static_assert(!noexcept(std::declval<const Jieba &>().cut_each(std::declval<std::string_view>(), CutMode::MIX,
                                                               std::declval<NoThrowWordVisitor>(),
                                                               std::declval<Workspace &>())));
