#pragma once

#include "neo/DictSource.hpp"
#include "neo/Jieba.hpp"
#include "neo/PosTag.hpp"
#include "neo/TokenView.hpp"
#include "neo/Traits.hpp"
#include "neo/Unicode.hpp"
#include "neo/Workspace.hpp"
#include "neo/detail/Logging.hpp"
#include "neo/detail/Output.hpp"
#include "neo/detail/StringUtil.hpp"
#include "neo/third_party/gtl.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace neo_cppjieba {

// One keyword in the input encoding with its score, its tag as Jieba::lookup_tag reports it,
// and every occurrence in input order.
// Positions use the same coordinates as Jieba::cut: source code units and runes, both half-open.
template <CharType CharT>
struct Keyword {
    std::basic_string<CharT> word;
    double weight{0.0};
    PosTag tag;
    std::vector<TokenPosition> positions;
};

// Python jieba's default allowPOS for textrank: place names, nouns, verbal nouns and verbs.
inline constexpr auto python_textrank_tags = std::array{pos::ns, pos::n, pos::vn, pos::v};

// An empty allowed_tags keeps every word, as CppJieba does. Otherwise only words whose tag is listed
// become candidates, like Python jieba's allowPOS; tags come from Jieba::lookup_tag.
struct TfIdfOptions {
    std::vector<PosTag> allowed_tags;
};

// CppJieba's TextRank defaults. Each candidate word links to the next window - 1 candidate words,
// so words removed by allowed_tags, like stop words, do not take up window slots.
struct TextRankOptions {
    size_t window{5};
    size_t iterations{10};
    double damping{0.85};
    std::vector<PosTag> allowed_tags;
};

namespace detail {

// u32string keys accept u32string_view lookups, so candidate words are never copied for a query.
struct RuneStringHash {
    using is_transparent = void;

    [[nodiscard]] auto operator()(std::u32string_view word) const noexcept -> size_t {
        return std::hash<std::u32string_view>{}(word);
    }
};

using RuneStringSet = gtl::flat_hash_set<std::u32string, RuneStringHash, std::equal_to<>>;
using IdfTable = gtl::flat_hash_map<std::u32string, double, RuneStringHash, std::equal_to<>>;

[[nodiscard]] inline auto rune_view(std::span<const Rune> runes) noexcept -> std::u32string_view {
    return {runes.data(), runes.size()};
}

// One word per line, compared exactly as written; empty lines are ignored.
inline auto load_stop_words(std::string_view text) -> RuneStringSet {
    auto words = RuneStringSet{};
    for (auto line : get_line_view(text)) {
        if (!line.empty()) {
            const auto runes = decode(line);
            words.emplace(runes.begin(), runes.end());
        }
    }
    return words;
}

// Each nonempty line is "word idf". Words absent from the table score with the average IDF, as in CppJieba.
inline auto load_idf(std::string_view text, std::string_view path, IdfTable &table) -> double {
    auto sum = 0.0;
    auto line_number = size_t{0};
    for (auto line : get_line_view(text)) {
        ++line_number;
        if (line.empty()) {
            continue;
        }
        auto fields = get_split_view(line, ' ');
        auto it = fields.begin();
        const auto word = *it;
        ++it;
        check(it != fields.end(), "KeywordExtractor: missing IDF at {}:{}", path, line_number);
        const auto idf = decode_value<double>(*it);
        ++it;
        check(it == fields.end(), "KeywordExtractor: expected exactly 2 fields at {}:{}", path, line_number);
        check(std::isfinite(idf), "KeywordExtractor: IDF must be finite at {}:{}", path, line_number);
        const auto runes = decode(word);
        table.insert_or_assign(std::u32string{runes.begin(), runes.end()}, idf);
        sum += idf;
    }
    check(!table.empty(), "KeywordExtractor: IDF table is empty: {}", path);
    return sum / static_cast<double>(table.size());
}

inline auto load_stop_words(const DictSource &source) -> RuneStringSet {
    return with_source_text(source, [](std::string_view text, std::string_view) { return load_stop_words(text); });
}

inline auto load_idf(const DictSource &source, IdfTable &table) -> double {
    return with_source_text(source,
                            [&](std::string_view text, std::string_view name) { return load_idf(text, name, table); });
}

// A distinct candidate word; ids follow first occurrence, which also breaks score ties.
struct KeywordCandidate {
    TokenPosition first;
    double weight;
    PosTag tag;
};

struct KeywordOccurrence {
    size_t candidate;
    TokenPosition position;
};

// A directed TextRank edge between node ranks, which order words as CppJieba's std::map<std::string> does.
struct KeywordEdge {
    size_t from;
    size_t to;
    double weight;
};

} // namespace detail

// Caller-owned storage for keyword extraction; reusable across inputs and both extractors.
// One active call owns the workspace; results never borrow its buffers.
class KeywordWorkspace {
public:
    KeywordWorkspace() = default;
    KeywordWorkspace(KeywordWorkspace &&) noexcept = default;
    auto operator=(KeywordWorkspace &&) noexcept -> KeywordWorkspace & = default;
    KeywordWorkspace(const KeywordWorkspace &) = delete;
    auto operator=(const KeywordWorkspace &) -> KeywordWorkspace & = delete;

    auto release() noexcept -> void {
        *this = KeywordWorkspace{};
    }

private:
    friend class TfIdfExtractor;
    friend class TextRankExtractor;

    // Segment once, then keep candidate words and their occurrences after stop-word and tag filtering.
    // A word's tag is looked up once, when it first passes the other filters.
    // Candidate keys borrow decoded_, so the index is cleared before decoded_ is reused.
    template <CharType CharT>
    auto collect(const Jieba &jieba, const detail::RuneStringSet &stop_words, std::span<const PosTag> allowed_tags,
                 std::basic_string_view<CharT> source) -> void {
        index_.clear();
        candidates_.clear();
        occurrences_.clear();
        decode_with_offset_into(source, decoded_);
        jieba.cut_runes_into(decoded_.runes, CutMode::MIX, ranges_, segment_);
        for (const auto range : ranges_) {
            const auto word = detail::rune_view(range.slice(decoded_.runes));
            if (word.size() == 1 || stop_words.contains(word)) {
                continue;
            }
            const auto position = TokenPosition{range, detail::to_source_range(range, decoded_.offsets)};
            auto it = index_.find(word);
            if (it == index_.end()) {
                const auto tag = jieba.lookup_tag(range.slice(decoded_.runes));
                if (!allowed_tags.empty() && std::ranges::find(allowed_tags, tag) == allowed_tags.end()) {
                    continue;
                }
                it = index_.emplace(word, candidates_.size()).first;
                candidates_.push_back({position, 0.0, tag});
            }
            occurrences_.push_back({it->second, position});
        }
    }

    [[nodiscard]] auto word(const detail::KeywordCandidate &candidate) const noexcept -> std::u32string_view {
        return detail::rune_view(candidate.first.runes.slice(decoded_.runes));
    }

    // Keep the top_n highest weights; equal weights keep first-occurrence order.
    // Output elements are overwritten in place so their strings and position arrays keep their capacity.
    template <CharType CharT>
    auto select(std::basic_string_view<CharT> source, size_t top_n, std::vector<Keyword<CharT>> &out) -> void {
        order_.resize(candidates_.size());
        for (auto id = size_t{0}; id < order_.size(); ++id) {
            order_[id] = id;
        }
        const auto count = std::min(top_n, order_.size());
        const auto ranked = [this](size_t lhs, size_t rhs) {
            const auto lhs_weight = candidates_[lhs].weight;
            const auto rhs_weight = candidates_[rhs].weight;
            return lhs_weight != rhs_weight ? lhs_weight > rhs_weight : lhs < rhs;
        };
        std::ranges::partial_sort(order_, order_.begin() + static_cast<std::ptrdiff_t>(count), ranked);

        constexpr auto unselected = std::numeric_limits<size_t>::max();
        slots_.assign(candidates_.size(), unselected);
        out.resize(count);
        for (auto slot = size_t{0}; slot < count; ++slot) {
            const auto &candidate = candidates_[order_[slot]];
            slots_[order_[slot]] = slot;
            out[slot].word.assign(candidate.first.source.slice(source));
            out[slot].weight = candidate.weight;
            out[slot].tag = candidate.tag;
            out[slot].positions.clear();
        }
        for (const auto &occurrence : occurrences_) {
            if (const auto slot = slots_[occurrence.candidate]; slot != unselected) {
                out[slot].positions.push_back(occurrence.position);
            }
        }
    }

    Workspace segment_;
    UnicodeWithOffset decoded_;
    std::vector<WordRange> ranges_;
    gtl::flat_hash_map<std::u32string_view, size_t, detail::RuneStringHash, std::equal_to<>> index_;
    std::vector<detail::KeywordCandidate> candidates_;
    std::vector<detail::KeywordOccurrence> occurrences_;
    std::vector<size_t> order_;
    std::vector<size_t> slots_;
    std::vector<size_t> ranks_;
    std::vector<detail::KeywordEdge> edges_;
    std::vector<size_t> edge_begin_;
    std::vector<double> out_sums_;
    std::vector<double> scores_;
};

// TF-IDF keywords, matching CppJieba's KeywordExtractor: segment with CutMode::MIX, drop one-rune words,
// stop words and words outside options.allowed_tags, then weight each distinct word by occurrence count times IDF.
// The extractor borrows jieba, which must outlive it; extraction is const and shares no mutable state.
class TfIdfExtractor {
public:
    TfIdfExtractor(const Jieba &jieba, std::string_view idf_path, std::string_view stop_words_path,
                   TfIdfOptions options = {})
        : TfIdfExtractor(jieba, DictSource::file(idf_path), DictSource::file(stop_words_path), std::move(options)) {
    }
    TfIdfExtractor(const Jieba &&, std::string_view, std::string_view, TfIdfOptions = {}) = delete;

    TfIdfExtractor(const Jieba &jieba, const DictSource &idf, const DictSource &stop_words, TfIdfOptions options = {})
        : jieba_(&jieba), idf_average_(detail::load_idf(idf, idf_)), stop_words_(detail::load_stop_words(stop_words)),
          options_(std::move(options)) {
    }
    TfIdfExtractor(const Jieba &&, const DictSource &, const DictSource &, TfIdfOptions = {}) = delete;

    template <StringLike Input>
    [[nodiscard]] auto extract(const Input &input, size_t top_n) const
        -> std::vector<Keyword<output_char_type_t<Input>>> {
        auto workspace = KeywordWorkspace{};
        auto result = std::vector<Keyword<output_char_type_t<Input>>>{};
        extract_into(input, top_n, result, workspace);
        return result;
    }

    // Replaces out, reusing its elements' capacity. Input must not alias out.
    template <StringLike Input>
    auto extract_into(const Input &input, size_t top_n, std::vector<Keyword<output_char_type_t<Input>>> &out,
                      KeywordWorkspace &workspace) const -> void {
        const auto source = as_view(input);
        workspace.collect(*jieba_, stop_words_, options_.allowed_tags, source);
        for (const auto &occurrence : workspace.occurrences_) {
            workspace.candidates_[occurrence.candidate].weight += 1.0;
        }
        for (auto &candidate : workspace.candidates_) {
            const auto it = idf_.find(workspace.word(candidate));
            candidate.weight *= it != idf_.end() ? it->second : idf_average_;
        }
        workspace.select(source, top_n, out);
    }

    // Words missing from the IDF file use the average of its values.
    [[nodiscard]] auto idf_average() const noexcept -> double {
        return idf_average_;
    }

    [[nodiscard]] auto options() const noexcept -> const TfIdfOptions & {
        return options_;
    }

private:
    const Jieba *jieba_;
    detail::IdfTable idf_;
    double idf_average_{0.0};
    detail::RuneStringSet stop_words_;
    TfIdfOptions options_;
};

// TextRank keywords, matching CppJieba's TextRankExtractor: candidates are filtered as in TfIdfExtractor,
// co-occurrence within the window adds undirected edges, and scores are normalized so the best is 1.
// The extractor borrows jieba, which must outlive it; extraction is const and shares no mutable state.
class TextRankExtractor {
public:
    TextRankExtractor(const Jieba &jieba, std::string_view stop_words_path, TextRankOptions options = {})
        : TextRankExtractor(jieba, DictSource::file(stop_words_path), std::move(options)) {
    }
    TextRankExtractor(const Jieba &&, std::string_view, TextRankOptions = {}) = delete;

    TextRankExtractor(const Jieba &jieba, const DictSource &stop_words, TextRankOptions options = {})
        : jieba_(&jieba), stop_words_(detail::load_stop_words(stop_words)), options_(std::move(options)) {
        check(options_.damping >= 0.0 && options_.damping <= 1.0, "TextRankExtractor: damping {} is outside [0, 1]",
              options_.damping);
    }
    TextRankExtractor(const Jieba &&, const DictSource &, TextRankOptions = {}) = delete;

    template <StringLike Input>
    [[nodiscard]] auto extract(const Input &input, size_t top_n) const
        -> std::vector<Keyword<output_char_type_t<Input>>> {
        auto workspace = KeywordWorkspace{};
        auto result = std::vector<Keyword<output_char_type_t<Input>>>{};
        extract_into(input, top_n, result, workspace);
        return result;
    }

    // Replaces out, reusing its elements' capacity. Input must not alias out.
    template <StringLike Input>
    auto extract_into(const Input &input, size_t top_n, std::vector<Keyword<output_char_type_t<Input>>> &out,
                      KeywordWorkspace &workspace) const -> void {
        const auto source = as_view(input);
        workspace.collect(*jieba_, stop_words_, options_.allowed_tags, source);
        rank(workspace);
        workspace.select(source, top_n, out);
    }

    [[nodiscard]] auto options() const noexcept -> const TextRankOptions & {
        return options_;
    }

private:
    // CppJieba updates scores in place, visiting nodes and neighbors in UTF-8 byte order of the words.
    // Ranks sort words by code point, which is the same order, so every floating-point step matches.
    auto rank(KeywordWorkspace &ws) const -> void {
        const auto candidate_count = ws.candidates_.size();
        auto &order = ws.order_;
        order.resize(candidate_count);
        for (auto id = size_t{0}; id < candidate_count; ++id) {
            order[id] = id;
        }
        std::ranges::sort(order, {}, [&](size_t id) { return ws.word(ws.candidates_[id]); });
        ws.ranks_.resize(candidate_count);
        for (auto rank = size_t{0}; rank < candidate_count; ++rank) {
            ws.ranks_[order[rank]] = rank;
        }

        // Each pair adds weight 1 in both directions; a word next to itself gets a self-loop of weight 2.
        auto &edges = ws.edges_;
        edges.clear();
        const auto &occurrences = ws.occurrences_;
        for (auto i = size_t{0}; i < occurrences.size(); ++i) {
            const auto from = ws.ranks_[occurrences[i].candidate];
            for (auto j = i + 1; j < occurrences.size() && j - i < options_.window; ++j) {
                const auto to = ws.ranks_[occurrences[j].candidate];
                edges.push_back({from, to, 1.0});
                edges.push_back({to, from, 1.0});
            }
        }
        if (edges.empty()) {
            return;
        }

        // Merge parallel edges into one adjacency array per node, neighbors in rank order.
        std::ranges::sort(edges, {}, [](const detail::KeywordEdge &edge) { return std::pair{edge.from, edge.to}; });
        auto merged = size_t{0};
        for (const auto &edge : edges) {
            if (merged > 0 && edges[merged - 1].from == edge.from && edges[merged - 1].to == edge.to) {
                edges[merged - 1].weight += edge.weight;
            } else {
                edges[merged++] = edge;
            }
        }
        edges.resize(merged);
        ws.edge_begin_.assign(candidate_count + 1, 0);
        for (const auto &edge : edges) {
            ++ws.edge_begin_[edge.from + 1];
        }
        auto node_count = size_t{0};
        for (auto rank = size_t{0}; rank < candidate_count; ++rank) {
            node_count += ws.edge_begin_[rank + 1] > 0 ? 1 : 0;
            ws.edge_begin_[rank + 1] += ws.edge_begin_[rank];
        }
        const auto neighbors = [&](size_t rank) {
            return std::span{edges}.subspan(ws.edge_begin_[rank], ws.edge_begin_[rank + 1] - ws.edge_begin_[rank]);
        };

        // Words without edges keep score 0 and still take part in normalization, as in CppJieba.
        auto &scores = ws.scores_;
        auto &out_sums = ws.out_sums_;
        scores.assign(candidate_count, 0.0);
        out_sums.assign(candidate_count, 0.0);
        const auto initial = 1.0 / static_cast<double>(node_count);
        for (auto rank = size_t{0}; rank < candidate_count; ++rank) {
            for (const auto &edge : neighbors(rank)) {
                scores[rank] = initial;
                out_sums[rank] += edge.weight;
            }
        }
        const auto damping = options_.damping;
        for (auto iteration = size_t{0}; iteration < options_.iterations; ++iteration) {
            for (auto rank = size_t{0}; rank < candidate_count; ++rank) {
                const auto adjacent = neighbors(rank);
                if (adjacent.empty()) {
                    continue;
                }
                auto sum = 0.0;
                for (const auto &edge : adjacent) {
                    sum += edge.weight / out_sums[edge.to] * scores[edge.to];
                }
                scores[rank] = (1 - damping) + damping * sum;
            }
        }

        const auto [min_it, max_it] = std::ranges::minmax_element(scores);
        const auto floor = *min_it / 10.0;
        for (auto rank = size_t{0}; rank < candidate_count; ++rank) {
            ws.candidates_[order[rank]].weight = (scores[rank] - floor) / (*max_it - floor);
        }
    }

    const Jieba *jieba_;
    detail::RuneStringSet stop_words_;
    TextRankOptions options_;
};

} // namespace neo_cppjieba
