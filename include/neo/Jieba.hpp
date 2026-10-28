#pragma once

#include "neo/Config.hpp"
#include "neo/CutMode.hpp"
#include "neo/DictSource.hpp"
#include "neo/Token.hpp"
#include "neo/Traits.hpp"
#include "neo/Unicode.hpp"
#include "neo/Workspace.hpp"
#include "neo/detail/DictTrie.hpp"
#include "neo/detail/HMModel.hpp"
#include "neo/detail/Logging.hpp"
#include "neo/detail/Output.hpp"
#include "neo/detail/Segmenter.hpp"
#include "neo/detail/Tagging.hpp"

#include <cstddef>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace neo_cppjieba {

namespace detail {

// Rune indices must fit WordRange, with room for the trailing offset sentinel.
inline auto assert_rune_count(size_t rune_count) noexcept -> void {
    assert_check(
        [&] { return size_fits_capacity<RuneIndex>(rune_count) && rune_count < std::numeric_limits<size_t>::max(); },
        "Jieba: input has {} runes, exceeding the configured index range", rune_count);
}

} // namespace detail

/// Jieba is the top-level facade for Chinese word segmentation. It owns the dictionary and HMM model;
/// every segmentation entry point is const and keeps per-call state in a Workspace.
/// Runtime word edits are non-const and need exclusive access; the library takes no locks.
///
/// Entry points differ only in how results are delivered:
///   - cut / cut_with_workspace: positions plus a borrowed view of the input (Tokens).
///   - cut_owned: positions plus the moved-in input (OwnedTokens).
///   - cut_strings: independent strings in the input's encoding.
///   - cut_into / cut_each: caller-owned output vectors or a visitor, reusing a Workspace.
///   - cut_runes / cut_runes_into: predecoded runes in, rune ranges out.
/// The algorithm behind each CutMode is selected in detail/Segmenter.hpp.
class Jieba {
public:
    // An empty user dictionary path explicitly disables user dictionary loading.
    explicit Jieba(std::string_view dict_path, std::string_view model_path, std::string_view user_dict_path)
        : dict_(dict_path, user_dict_path), model_(model_path) {
    }

    // Load from files or memory; user dictionaries load in order and a repeated word takes the last entry.
    explicit Jieba(const DictSource &dict, const DictSource &model, std::span<const DictSource> user_dicts = {})
        : dict_(dict, user_dicts), model_(model) {
    }

    // Tagging::Pos makes an entry point attach each word's part-of-speech tag without changing segmentation:
    // the dictionary tag when the word has one, otherwise CppJieba's rule ("m", "eng" or "x").
    // Tags are looked up only after segmentation, so Tagging::None instantiations do no tagging work.

    // Borrowing rejects owning temporaries; callers keep the original text alive and stable.
    template <Tagging tagging = Tagging::None, StringViewSource Input>
    [[nodiscard]] auto cut(Input &&input, CutMode mode) const -> Tokens<output_char_type_t<Input>, tagging> {
        auto workspace = Workspace{};
        return cut_with_workspace<tagging>(std::forward<Input>(input), mode, workspace);
    }

    // Caller-owned workspace may be reused; each result owns its positions and borrows only the input.
    // Convert the input once so decoding and the returned view refer to the same source.
    template <Tagging tagging = Tagging::None, StringViewSource Input>
    [[nodiscard]] auto cut_with_workspace(Input &&input, CutMode mode, Workspace &workspace) const
        -> Tokens<output_char_type_t<Input>, tagging> {
        const auto source = as_view(std::forward<Input>(input));
        auto positions = std::vector<detail::token_record_t<tagging>>{};
        cut_into(source, mode, positions, workspace);
        return Tokens<output_char_type_t<Input>, tagging>{source, std::move(positions)};
    }

    // Replaces output while retaining its capacity. Output records contain no decoding-buffer pointers.
    // Caller-owned decoding buffers can be reused across inputs; results never borrow this storage.
    // Input must not alias the output array or either decoding buffer.
    // The workspace also retains route, HMM, separator, and rune-result storage.
    // TaggedPosition and TaggedWord outputs select tagging; other outputs are untagged.
    template <StringLike Input, typename Output>
        requires detail::OutputValue<Output, output_char_type_t<Input>>
                 || detail::TaggedOutputValue<Output, output_char_type_t<Input>>
    auto cut_into(const Input &input, CutMode mode, std::vector<Output> &out, Workspace &workspace) const -> void {
        out.clear();
        const auto source = as_view(input);
        const auto ranges = prepare_and_cut(source, mode, workspace);
        if constexpr (detail::TaggedOutputValue<Output, output_char_type_t<Input>>) {
            detail::fill_tagged_output(dict_, source, workspace.decoded_.runes, ranges, workspace.decoded_.offsets,
                                       out);
        } else {
            detail::fill_output(source, ranges, workspace.decoded_.offsets, out);
        }
    }

    // No public token array is built; segmenters still compute rune ranges first.
    // Visitors may throw, but must not reuse this decoding buffer or invalidate the input text.
    template <Tagging tagging = Tagging::None, StringLike Input, typename Emit>
        requires detail::ViewVisitor<Emit, detail::token_view_t<tagging, output_char_type_t<Input>>>
    auto cut_each(const Input &input, CutMode mode, Emit &&emit, Workspace &workspace) const -> void {
        const auto source = as_view(input);
        const auto ranges = prepare_and_cut(source, mode, workspace);
        if constexpr (tagging == Tagging::Pos) {
            detail::emit_tagged_tokens(dict_, source, workspace.decoded_.runes, ranges, workspace.decoded_.offsets,
                                       emit);
        } else {
            detail::emit_tokens(source, ranges, workspace.decoded_.offsets, emit);
        }
    }

    // Move the source into its final owner before building positions, including for small strings.
    template <Tagging tagging = Tagging::None, CharType CharT>
    [[nodiscard]] auto cut_owned(std::basic_string<CharT> source, CutMode mode) const -> OwnedTokens<CharT, tagging> {
        auto result = OwnedTokens<CharT, tagging>{std::move(source), {}};
        auto workspace = Workspace{};
        cut_into(result.source_, mode, result.positions_, workspace);
        return result;
    }

    // UTF-8 byte buffers produce std::string words; character inputs retain their native string type.
    // Tagging::Pos returns word/tag pairs, like CppJieba's Tag and Python jieba's posseg.lcut.
    template <Tagging tagging = Tagging::None, StringLike Input>
    [[nodiscard]] auto cut_strings(const Input &input, CutMode mode) const
        -> std::vector<detail::word_t<tagging, output_char_type_t<Input>>> {
        auto workspace = Workspace{};
        auto result = std::vector<detail::word_t<tagging, output_char_type_t<Input>>>{};
        cut_into(input, mode, result, workspace);
        return result;
    }

    // Predecoded input retains rune coordinates and never constructs source text or an offset table.
    [[nodiscard]] auto cut_runes(std::span<const Rune> runes, CutMode mode) const -> std::vector<WordRange> {
        auto result = std::vector<WordRange>{};
        auto workspace = Workspace{};
        cut_runes_into(runes, mode, result, workspace);
        return result;
    }

    // Fill the caller's rune output directly; decoding storage is unused for predecoded input.
    // A failure propagates and may leave partial output.
    auto cut_runes_into(std::span<const Rune> runes, CutMode mode, std::vector<WordRange> &out,
                        Workspace &workspace) const -> void {
        out.clear();
        detail::check_cut_mode(mode);
        detail::assert_rune_count(runes.size());
        detail::segment_into(dict_, model_, runes, mode, out, workspace.scratch_);
    }

    // Tag one complete word without segmenting it, like CppJieba's LookupTag.
    [[nodiscard]] auto lookup_tag(std::span<const Rune> word) const noexcept -> PosTag {
        return detail::word_tag(dict_, word);
    }

    template <StringLike Input>
    [[nodiscard]] auto lookup_tag(const Input &word) const -> PosTag {
        const auto runes = decode(word);
        return lookup_tag(std::span<const Rune>{runes});
    }

    // Runtime words behave like user dictionary entries and keep the load-time frequency total.
    // Callers serialize edits against all other calls, e.g. with a lock, copy-on-write, or a single thread.
    template <StringLike Input>
    auto add_word(const Input &word, PosTag tag) -> void {
        const auto runes = decode(word);
        dict_.add_word(runes, tag);
    }

    template <StringLike Input>
    auto add_word_with_freq(const Input &word, int freq, PosTag tag) -> void {
        const auto runes = decode(word);
        dict_.add_word_with_freq(runes, freq, tag);
    }

    // Removal drops main and user dictionary words alike; returns false when the word is absent.
    template <StringLike Input>
    auto remove_word(const Input &word) -> bool {
        const auto runes = decode(word);
        return dict_.remove_word(runes);
    }

private:
    template <CharType CharT>
    auto prepare_and_cut(std::basic_string_view<CharT> source, CutMode mode, Workspace &workspace) const
        -> std::span<const WordRange> {
        detail::check_cut_mode(mode);
        decode_with_offset_into(source, workspace.decoded_);
        // Decoding bounds source offsets; the rune count cannot exceed the source code-unit count.
        // Rune and source capacities may be configured independently.
        detail::assert_rune_count(workspace.decoded_.runes.size());
        detail::segment_into(dict_, model_, workspace.decoded_.runes, mode, workspace.ranges_, workspace.scratch_);
        return workspace.ranges_;
    }

    detail::DictTrie dict_;
    detail::HMModel model_;
};

} // namespace neo_cppjieba
