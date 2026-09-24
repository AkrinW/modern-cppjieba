#include "neo/DictTrie.hpp"
#include "neo/FullSegment.hpp"
#include "neo/HMMSegment.hpp"
#include "neo/HMModel.hpp"
#include "neo/Jieba.hpp"
#include "neo/MPSegment.hpp"
#include "neo/MixSegment.hpp"
#include "neo/QuerySegment.hpp"

#include <concepts>
#include <cstdint>
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
inline constexpr bool
    jieba_cut_invocable_v<Input, std::void_t<decltype(std::declval<const Jieba &>().cut(std::declval<Input>()))>> =
        true;

// Mode validation must participate in overload selection for every public input family.
template <CutMethod Method, typename Input>
concept JiebaMethodInvocable = requires { std::declval<const Jieba &>().template cut<Method>(std::declval<Input>()); };

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

static_assert(JiebaMethodInvocable<CutMethod::MIX, std::string_view>);
static_assert(JiebaMethodInvocable<CutMethod::FULL, std::string_view>);
static_assert(JiebaMethodInvocable<CutMethod::SEARCH, std::string_view>);
static_assert(JiebaMethodInvocable<CutMethod::HMM, std::string_view>);
static_assert(JiebaMethodInvocable<CutMethod::MP, std::string_view>);
static_assert(!JiebaMethodInvocable<static_cast<CutMethod>(uint8_t{255}), std::span<const Rune>>);
static_assert(!JiebaMethodInvocable<static_cast<CutMethod>(uint8_t{255}), Unicode>);
static_assert(!JiebaMethodInvocable<static_cast<CutMethod>(uint8_t{255}), UnicodeWithOffset>);
static_assert(!JiebaMethodInvocable<static_cast<CutMethod>(uint8_t{255}), std::string_view>);

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
static_assert(std::same_as<decltype(std::declval<const Jieba &>().cut(std::declval<std::span<const Rune>>())),
                           std::vector<WordRange>>);
static_assert(
    std::same_as<decltype(std::declval<const Jieba &>().cut(std::declval<std::span<Rune>>())), std::vector<WordRange>>);
static_assert(std::same_as<decltype(std::declval<const Jieba &>().cut(std::declval<std::span<Rune, 5>>())),
                           std::vector<WordRange>>);
static_assert(std::same_as<decltype(std::declval<const Jieba &>().cut(std::declval<std::span<const Rune, 5>>())),
                           std::vector<WordRange>>);
static_assert(std::same_as<decltype(std::declval<const Jieba &>().cut(std::declval<std::span<Rune, 0>>())),
                           std::vector<WordRange>>);
static_assert(std::same_as<decltype(std::declval<const Jieba &>().cut(std::declval<std::span<const Rune, 0>>())),
                           std::vector<WordRange>>);
static_assert(std::same_as<decltype(std::declval<const Jieba &>().cut(std::declval<std::string_view>())),
                           std::vector<std::string>>);
static_assert(std::same_as<decltype(std::declval<const Jieba &>().cut(std::declval<std::u32string_view>())),
                           std::vector<std::u32string>>);
