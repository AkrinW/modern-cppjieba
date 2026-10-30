#pragma once

#include <stdint.h>

/* Static consumers define NEO_CPPJIEBA_C_STATIC; the CMake static target supplies it automatically.
 * NEO_CPPJIEBA_C_BUILD is private to the shared-library build. */
#if defined(NEO_CPPJIEBA_C_STATIC)
#define NEO_CPPJIEBA_C_API
#elif defined(_WIN32)
#if defined(NEO_CPPJIEBA_C_BUILD)
#define NEO_CPPJIEBA_C_API __declspec(dllexport)
#else
#define NEO_CPPJIEBA_C_API __declspec(dllimport)
#endif
#else
#define NEO_CPPJIEBA_C_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* V1 layouts and numeric values are frozen within one platform ABI. Structs are passed by pointer.
 * All pointers must address live, correctly sized storage; input, output and error buffers must not alias.
 * Threads may share engines and results, not outputs or error buffers; finish all uses before destroying a handle. */

typedef int32_t neo_jieba_status_v1;
enum {
    NEO_JIEBA_V1_OK = 0,
    NEO_JIEBA_V1_INVALID_ARGUMENT = 1,
    NEO_JIEBA_V1_OPERATION_FAILED = 2,
    NEO_JIEBA_V1_OUT_OF_MEMORY = 3,
    NEO_JIEBA_V1_LIMIT_EXCEEDED = 4
};

typedef uint32_t neo_jieba_mode_v1;
enum {
    NEO_JIEBA_V1_MIX = 1,
    NEO_JIEBA_V1_MIX_NO_HMM = 2,
    NEO_JIEBA_V1_FULL = 3,
    NEO_JIEBA_V1_SEARCH = 4,
    NEO_JIEBA_V1_SEARCH_NO_HMM = 5,
    NEO_JIEBA_V1_HMM = 6,
    NEO_JIEBA_V1_MP = 7
};

typedef uint32_t neo_jieba_source_kind_v1;
enum { NEO_JIEBA_V1_SOURCE_FILE = 1, NEO_JIEBA_V1_SOURCE_MEMORY = 2 };

/* Owns dictionary/model storage. Destroy through the same library instance that created it.
 * engine_add_word, engine_add_word_with_freq, engine_remove_word and engine_compact need exclusive access;
 * every other call only reads the engine. */
typedef struct neo_jieba_engine_v1 neo_jieba_engine_v1;

/* Owns a copy of the input and all token ranges, independently of the engine and other results. */
typedef struct neo_jieba_tokens_v1 neo_jieba_tokens_v1;

/* Owns reusable segmentation and keyword scratch storage and never holds results; it serves one call at a time. */
typedef struct neo_jieba_workspace_v1 neo_jieba_workspace_v1;

/* Owns reusable byte-range and tag arrays for the input of the call that last filled it; it never copies that input. */
typedef struct neo_jieba_ranges_v1 neo_jieba_ranges_v1;

/* Owns TF-IDF keyword resources, an IDF table and stop words; extraction only reads it. */
typedef struct neo_jieba_tfidf_v1 neo_jieba_tfidf_v1;

/* Owns TextRank keyword resources, the stop words; extraction only reads it. */
typedef struct neo_jieba_textrank_v1 neo_jieba_textrank_v1;

/* Owns reusable keyword and occurrence arrays for the input of the extraction that last filled it. */
typedef struct neo_jieba_keywords_v1 neo_jieba_keywords_v1;

/* Non-null, NUL-terminated native file paths borrowed only during engine_create.
 * Dictionary/model paths must be nonempty; an empty user_dict_path disables the user dictionary. */
typedef struct neo_jieba_paths_v1 {
    const char *dict_path;
    const char *model_path;
    const char *user_dict_path;
} neo_jieba_paths_v1;

/* One dictionary, model or snapshot input, borrowed only during the call that reads it; reserved must be zero.
 * FILE data holds a nonempty path of size bytes without NUL, read like engine_create paths; MEMORY data holds the
 * contents such a file would have. NULL data is accepted exactly when size is zero. */
typedef struct neo_jieba_source_v1 {
    neo_jieba_source_kind_v1 kind;
    uint32_t reserved;
    const char *data;
    uint64_t size;
} neo_jieba_source_v1;

/* Caller-owned diagnostic storage; pass NULL to omit diagnostics, or {NULL, 0, 0} to obtain the length.
 * message_size receives the full byte length excluding NUL; capacity > 0 always receives a NUL-terminated prefix.
 * Successful calls clear the message. Diagnostic truncation does not change the operation's status. */
typedef struct neo_jieba_error_buffer_v1 {
    char *data;
    uint64_t capacity;
    uint64_t message_size;
} neo_jieba_error_buffer_v1;

/* A byte range in the result's original UTF-8 text. Full/search modes may emit overlapping ranges. */
typedef struct neo_jieba_token_range_v1 {
    uint64_t byte_offset;
    uint64_t byte_length;
} neo_jieba_token_range_v1;

/* A part-of-speech tag of up to four bytes, NUL-padded; a four-byte tag has no terminating NUL. */
typedef struct neo_jieba_pos_tag_v1 {
    char data[4];
} neo_jieba_pos_tag_v1;

/* Read-only storage borrowed from a result until tokens_destroy; never free these pointers.
 * Text may contain NUL bytes; lengths are authoritative, and individual tokens are not NUL-terminated.
 * Empty text/range arrays have NULL pointers and zero lengths. */
typedef struct neo_jieba_borrowed_tokens_v1 {
    const char *utf8_data;
    uint64_t utf8_size;
    const neo_jieba_token_range_v1 *ranges;
    uint64_t count;
} neo_jieba_borrowed_tokens_v1;

/* Read-only arrays borrowed from a ranges result until its next fill or ranges_destroy; never free these pointers.
 * Offsets index the input passed to that fill. Empty arrays are NULL; tags is NULL after an untagged fill and
 * otherwise holds count tags parallel to ranges. */
typedef struct neo_jieba_borrowed_ranges_v1 {
    const neo_jieba_token_range_v1 *ranges;
    uint64_t count;
    const neo_jieba_pos_tag_v1 *tags;
} neo_jieba_borrowed_ranges_v1;

/* NULL allowed_tags with a zero count keeps every candidate word; otherwise only words whose tag is listed remain.
 * Tags must be NUL-padded and are compared with the tag lookup_tag reports. */
typedef struct neo_jieba_tfidf_options_v1 {
    const neo_jieba_pos_tag_v1 *allowed_tags;
    uint64_t allowed_tag_count;
} neo_jieba_tfidf_options_v1;

/* Each candidate links to the next window - 1 candidates; iterations rounds run with damping in [0, 1].
 * CppJieba uses window 5, iterations 10 and damping 0.85. allowed_tags filters as in neo_jieba_tfidf_options_v1. */
typedef struct neo_jieba_textrank_options_v1 {
    uint64_t window;
    uint64_t iterations;
    double damping;
    const neo_jieba_pos_tag_v1 *allowed_tags;
    uint64_t allowed_tag_count;
} neo_jieba_textrank_options_v1;

/* One keyword: its weight, its tag as lookup_tag reports it, and occurrence_count byte ranges starting at
 * first_occurrence in the borrowed occurrence array, in input order. The first range spells the word; reserved is 0. */
typedef struct neo_jieba_keyword_v1 {
    double weight;
    uint64_t first_occurrence;
    uint64_t occurrence_count;
    neo_jieba_pos_tag_v1 tag;
    uint32_t reserved;
} neo_jieba_keyword_v1;

/* Read-only arrays borrowed from a keywords result until its next fill or keywords_destroy; never free them.
 * Keywords run from the highest weight, ties in first-occurrence order. Ranges index that fill's input. */
typedef struct neo_jieba_borrowed_keywords_v1 {
    const neo_jieba_keyword_v1 *keywords;
    uint64_t count;
    const neo_jieba_token_range_v1 *occurrences;
    uint64_t occurrence_count;
} neo_jieba_borrowed_keywords_v1;

/* Returns a conservative byte limit for this build's source/rune indices; allocation can still fail. */
NEO_CPPJIEBA_C_API uint64_t neo_jieba_v1_max_input_bytes(void);

/* On success transfers one engine to out_owned_engine; on failure sets it to NULL.
 * The output slot must not hold an unreleased owner. File/format failures return OPERATION_FAILED. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_engine_create(const neo_jieba_paths_v1 *paths,
                                                                  neo_jieba_engine_v1 **out_owned_engine,
                                                                  neo_jieba_error_buffer_v1 *error);

/* Releases an engine; NULL is accepted. Existing token and ranges results remain valid. */
NEO_CPPJIEBA_C_API void neo_jieba_v1_engine_destroy(neo_jieba_engine_v1 *owned_engine);

/* Borrows input only during this call; NULL is accepted exactly when input_size is zero.
 * On success transfers an independent result to out_owned_tokens; on failure sets it to NULL.
 * Invalid UTF-8 returns OPERATION_FAILED. The output slot must not hold an unreleased owner. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_cut_utf8_owned(const neo_jieba_engine_v1 *engine, const char *input,
                                                                   uint64_t input_size, neo_jieba_mode_v1 mode,
                                                                   neo_jieba_tokens_v1 **out_owned_tokens,
                                                                   neo_jieba_error_buffer_v1 *error);

/* Copies a borrowed descriptor into caller storage; a valid output is zeroed on failure.
 * This transfers no ownership and never invalidates earlier borrowed descriptors. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_tokens_borrow_data(const neo_jieba_tokens_v1 *tokens,
                                                                       neo_jieba_borrowed_tokens_v1 *out_borrowed,
                                                                       neo_jieba_error_buffer_v1 *error);

/* Releases a result and invalidates all its borrowed pointers; NULL is accepted.
 * Release all handles before unloading their library. Internal invariant failures remain fatal. */
NEO_CPPJIEBA_C_API void neo_jieba_v1_tokens_destroy(neo_jieba_tokens_v1 *owned_tokens);

/* On success transfers one workspace to out_owned_workspace; on failure sets it to NULL.
 * Scratch capacity grows with use and is retained until workspace_destroy. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_workspace_create(neo_jieba_workspace_v1 **out_owned_workspace,
                                                                     neo_jieba_error_buffer_v1 *error);

/* Releases a workspace; NULL is accepted. Ranges filled through it remain valid. */
NEO_CPPJIEBA_C_API void neo_jieba_v1_workspace_destroy(neo_jieba_workspace_v1 *owned_workspace);

/* On success transfers one empty ranges result to out_owned_ranges; on failure sets it to NULL. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_ranges_create(neo_jieba_ranges_v1 **out_owned_ranges,
                                                                  neo_jieba_error_buffer_v1 *error);

/* Releases a ranges result and invalidates its borrowed pointer; NULL is accepted. */
NEO_CPPJIEBA_C_API void neo_jieba_v1_ranges_destroy(neo_jieba_ranges_v1 *owned_ranges);

/* Replaces ranges with byte ranges into input, reusing workspace and ranges capacity. Input is read only during this
 * call and never copied; NULL is accepted exactly when input_size is zero. Invalid UTF-8 returns OPERATION_FAILED,
 * and any failure leaves ranges empty. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_cut_utf8_into(const neo_jieba_engine_v1 *engine, const char *input,
                                                                  uint64_t input_size, neo_jieba_mode_v1 mode,
                                                                  neo_jieba_workspace_v1 *workspace,
                                                                  neo_jieba_ranges_v1 *ranges,
                                                                  neo_jieba_error_buffer_v1 *error);

/* Copies a borrowed descriptor into caller storage; a valid output is zeroed on failure. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_ranges_borrow_data(const neo_jieba_ranges_v1 *ranges,
                                                                       neo_jieba_borrowed_ranges_v1 *out_borrowed,
                                                                       neo_jieba_error_buffer_v1 *error);

/* Loads the main dictionary, the HMM model and user dictionaries in order; a repeated user word takes the last entry.
 * user_dicts may be NULL exactly when user_dict_count is zero. Every source is checked before loading starts.
 * Transfers one engine on success, otherwise sets out_owned_engine to NULL. Load failures return OPERATION_FAILED. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_engine_create_from_sources(
    const neo_jieba_source_v1 *dict, const neo_jieba_source_v1 *model, const neo_jieba_source_v1 *user_dicts,
    uint64_t user_dict_count, neo_jieba_engine_v1 **out_owned_engine, neo_jieba_error_buffer_v1 *error);

/* Loads the dictionary from a snapshot made by write_dict_snapshot and the HMM model from model; the engine cuts
 * exactly as one built from the snapshot's sources. Snapshots from earlier releases stay readable. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1
neo_jieba_v1_engine_create_from_snapshot(const neo_jieba_source_v1 *dict_snapshot, const neo_jieba_source_v1 *model,
                                         neo_jieba_engine_v1 **out_owned_engine, neo_jieba_error_buffer_v1 *error);

/* Encodes the dictionary engine_create_from_sources would build from dict and user_dicts and publishes it at the path
 * of path_size bytes atomically: readers see the previous file or the complete snapshot, never a partial write. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_write_dict_snapshot(const neo_jieba_source_v1 *dict,
                                                                        const neo_jieba_source_v1 *user_dicts,
                                                                        uint64_t user_dict_count, const char *path,
                                                                        uint64_t path_size,
                                                                        neo_jieba_error_buffer_v1 *error);

/* Like cut_utf8_into, and also fills one tag per range without changing segmentation: the word's dictionary tag,
 * otherwise CppJieba's rule ("m" for digits, "eng" for other ASCII, else "x"). Any failure leaves both arrays empty. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_cut_utf8_tagged_into(
    const neo_jieba_engine_v1 *engine, const char *input, uint64_t input_size, neo_jieba_mode_v1 mode,
    neo_jieba_workspace_v1 *workspace, neo_jieba_ranges_v1 *ranges, neo_jieba_error_buffer_v1 *error);

/* Tags one complete UTF-8 word without segmenting it, exactly as a tagged fill would tag that word. NULL is accepted
 * exactly when word_size is zero. Invalid UTF-8 returns OPERATION_FAILED; a valid out_tag is zeroed on failure. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_lookup_tag(const neo_jieba_engine_v1 *engine, const char *word,
                                                               uint64_t word_size, neo_jieba_pos_tag_v1 *out_tag,
                                                               neo_jieba_error_buffer_v1 *error);

/* Copies engine into an independent engine for copy-on-write edits; it only reads engine, so it may overlap reads.
 * On success transfers the copy to out_owned_engine; on failure sets it to NULL. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_engine_clone(const neo_jieba_engine_v1 *engine,
                                                                 neo_jieba_engine_v1 **out_owned_engine,
                                                                 neo_jieba_error_buffer_v1 *error);

/* Adds or replaces a nonempty UTF-8 word as a user word with the default user weight; an all-zero tag is empty.
 * Tags must be NUL-padded. Needs exclusive access to engine; existing results stay valid. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_engine_add_word(neo_jieba_engine_v1 *engine, const char *word,
                                                                    uint64_t word_size, const neo_jieba_pos_tag_v1 *tag,
                                                                    neo_jieba_error_buffer_v1 *error);

/* Like engine_add_word, weighted by a positive frequency against the main dictionary total fixed at load time. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_engine_add_word_with_freq(neo_jieba_engine_v1 *engine,
                                                                              const char *word, uint64_t word_size,
                                                                              int32_t freq,
                                                                              const neo_jieba_pos_tag_v1 *tag,
                                                                              neo_jieba_error_buffer_v1 *error);

/* Removes a main or user dictionary word without restoring an earlier weight; needs exclusive access to engine.
 * out_removed receives 1 when the word was present and 0 otherwise, including on failure. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_engine_remove_word(neo_jieba_engine_v1 *engine, const char *word,
                                                                       uint64_t word_size, uint32_t *out_removed,
                                                                       neo_jieba_error_buffer_v1 *error);

/* Reclaims storage left by edits without changing any result; needs exclusive access to engine. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_engine_compact(neo_jieba_engine_v1 *engine,
                                                                   neo_jieba_error_buffer_v1 *error);

/* Loads an IDF table and stop words; words missing from the table use its average IDF. On success transfers one
 * extractor to out_owned_extractor; on failure sets it to NULL. Load failures return OPERATION_FAILED. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_tfidf_create(const neo_jieba_source_v1 *idf,
                                                                 const neo_jieba_source_v1 *stop_words,
                                                                 neo_jieba_tfidf_v1 **out_owned_extractor,
                                                                 neo_jieba_error_buffer_v1 *error);

/* Releases a TF-IDF extractor; NULL is accepted. Keyword results remain valid. */
NEO_CPPJIEBA_C_API void neo_jieba_v1_tfidf_destroy(neo_jieba_tfidf_v1 *owned_extractor);

/* Loads TextRank stop words. On success transfers one extractor to out_owned_extractor; on failure sets it to NULL. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_textrank_create(const neo_jieba_source_v1 *stop_words,
                                                                    neo_jieba_textrank_v1 **out_owned_extractor,
                                                                    neo_jieba_error_buffer_v1 *error);

/* Releases a TextRank extractor; NULL is accepted. Keyword results remain valid. */
NEO_CPPJIEBA_C_API void neo_jieba_v1_textrank_destroy(neo_jieba_textrank_v1 *owned_extractor);

/* On success transfers one empty keywords result to out_owned_keywords; on failure sets it to NULL. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_keywords_create(neo_jieba_keywords_v1 **out_owned_keywords,
                                                                    neo_jieba_error_buffer_v1 *error);

/* Releases a keywords result and invalidates its borrowed pointers; NULL is accepted. */
NEO_CPPJIEBA_C_API void neo_jieba_v1_keywords_destroy(neo_jieba_keywords_v1 *owned_keywords);

/* Replaces keywords with at most top_n TF-IDF keywords of input, segmented by engine in MIX mode, reusing workspace
 * and keywords capacity. Input is never copied. Invalid UTF-8 returns OPERATION_FAILED; failures leave it empty. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_tfidf_extract_into(
    const neo_jieba_tfidf_v1 *extractor, const neo_jieba_engine_v1 *engine, const char *input, uint64_t input_size,
    uint64_t top_n, const neo_jieba_tfidf_options_v1 *options, neo_jieba_workspace_v1 *workspace,
    neo_jieba_keywords_v1 *keywords, neo_jieba_error_buffer_v1 *error);

/* Like tfidf_extract_into, ranking candidates by TextRank as CppJieba's TextRankExtractor does. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_textrank_extract_into(
    const neo_jieba_textrank_v1 *extractor, const neo_jieba_engine_v1 *engine, const char *input, uint64_t input_size,
    uint64_t top_n, const neo_jieba_textrank_options_v1 *options, neo_jieba_workspace_v1 *workspace,
    neo_jieba_keywords_v1 *keywords, neo_jieba_error_buffer_v1 *error);

/* Copies a borrowed descriptor into caller storage; a valid output is zeroed on failure. */
NEO_CPPJIEBA_C_API neo_jieba_status_v1 neo_jieba_v1_keywords_borrow_data(const neo_jieba_keywords_v1 *keywords,
                                                                         neo_jieba_borrowed_keywords_v1 *out_borrowed,
                                                                         neo_jieba_error_buffer_v1 *error);

#ifdef __cplusplus
}
#endif
