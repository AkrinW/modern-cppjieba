#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* V1 layouts and numeric values are frozen within one platform ABI. Structs are passed by pointer.
 * All pointers must address live, correctly sized storage; input, output and error buffers must not alias.
 * Serialize calls using the same engine, and finish all reads before destroying a result. */

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

/* Owns dictionary/model storage. Destroy through the same library instance that created it. */
typedef struct neo_jieba_engine_v1 neo_jieba_engine_v1;

/* Owns a copy of the input and all token ranges, independently of the engine and other results. */
typedef struct neo_jieba_tokens_v1 neo_jieba_tokens_v1;

/* Non-null, NUL-terminated native file paths borrowed only during engine_create.
 * Dictionary/model paths must be nonempty; an empty user_dict_path disables the user dictionary. */
typedef struct neo_jieba_paths_v1 {
    const char *dict_path;
    const char *model_path;
    const char *user_dict_path;
} neo_jieba_paths_v1;

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

/* Read-only storage borrowed from a result until tokens_destroy; never free these pointers.
 * Text may contain NUL bytes; lengths are authoritative, and individual tokens are not NUL-terminated.
 * Empty text/range arrays have NULL pointers and zero lengths. */
typedef struct neo_jieba_borrowed_tokens_v1 {
    const char *utf8_data;
    uint64_t utf8_size;
    const neo_jieba_token_range_v1 *ranges;
    uint64_t count;
} neo_jieba_borrowed_tokens_v1;

/* Returns a conservative byte limit for this build's source/rune indices; allocation can still fail. */
__attribute__((visibility("default"))) uint64_t neo_jieba_v1_max_input_bytes(void);

/* On success transfers one engine to out_owned_engine; on failure sets it to NULL.
 * The output slot must not hold an unreleased owner. File/format failures return OPERATION_FAILED. */
__attribute__((visibility("default"))) neo_jieba_status_v1 neo_jieba_v1_engine_create(
    const neo_jieba_paths_v1 *paths, neo_jieba_engine_v1 **out_owned_engine, neo_jieba_error_buffer_v1 *error);

/* Releases an engine; NULL is accepted. Existing token results remain valid. */
__attribute__((visibility("default"))) void neo_jieba_v1_engine_destroy(neo_jieba_engine_v1 *owned_engine);

/* Borrows input only during this call; NULL is accepted exactly when input_size is zero.
 * On success transfers an independent result to out_owned_tokens; on failure sets it to NULL.
 * Invalid UTF-8 returns OPERATION_FAILED. The output slot must not hold an unreleased owner. */
__attribute__((visibility("default"))) neo_jieba_status_v1 neo_jieba_v1_cut_utf8_owned(
    const neo_jieba_engine_v1 *engine, const char *input, uint64_t input_size, neo_jieba_mode_v1 mode,
    neo_jieba_tokens_v1 **out_owned_tokens, neo_jieba_error_buffer_v1 *error);

/* Copies a borrowed descriptor into caller storage; a valid output is zeroed on failure.
 * This transfers no ownership and never invalidates earlier borrowed descriptors. */
__attribute__((visibility("default"))) neo_jieba_status_v1 neo_jieba_v1_tokens_borrow_data(
    const neo_jieba_tokens_v1 *tokens, neo_jieba_borrowed_tokens_v1 *out_borrowed, neo_jieba_error_buffer_v1 *error);

/* Releases a result and invalidates all its borrowed pointers; NULL is accepted.
 * Release all handles before unloading their library. Internal invariant failures remain fatal. */
__attribute__((visibility("default"))) void neo_jieba_v1_tokens_destroy(neo_jieba_tokens_v1 *owned_tokens);

#ifdef __cplusplus
}
#endif
