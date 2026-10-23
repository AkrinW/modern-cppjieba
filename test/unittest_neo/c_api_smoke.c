#include "neo_cppjieba.h"
#include "test_paths.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(neo_jieba_status_v1) == 4, "Status ABI must remain 32-bit");
_Static_assert(sizeof(neo_jieba_mode_v1) == 4, "Mode ABI must remain 32-bit");
_Static_assert(sizeof(neo_jieba_token_range_v1) == 16, "Token ABI must remain two uint64_t fields");
_Static_assert(offsetof(neo_jieba_token_range_v1, byte_length) == 8, "Token field offset changed");

/* Export hostile C++ runtime symbols from this C-only host to detect accidental interposition. */
#ifdef NEO_CPPJIEBA_C_TEST_PRIVATE_RUNTIME
#if UINTPTR_MAX == UINT64_MAX
void *host_cpp_new(size_t size) __asm__("_Znwm");
#else
void *host_cpp_new(size_t size) __asm__("_Znwj");
#endif

/* Private C++ allocation inside the library must never reach the host's operator new. */
void *host_cpp_new(size_t size) {
    (void)size;
    abort();
}

/* A malformed UTF-8 input exercises exception construction and dispatch inside the shared library. */
void host_cpp_throw(void *exception, void *type, void (*destroy)(void *)) __asm__("__cxa_throw");

/* Private exception handling must never use the host's incompatible runtime entry point. */
void host_cpp_throw(void *exception, void *type, void (*destroy)(void *)) {
    (void)exception;
    (void)type;
    (void)destroy;
    abort();
}
#endif

/* Compile and link as C, exercising owned results and an error with conflicting host symbols present. */
int main(void) {
    const neo_jieba_paths_v1 paths = {DICT_DIR "/jieba.dict.utf8", DICT_DIR "/hmm_model.utf8", ""};
    char message[512] = {0};
    neo_jieba_error_buffer_v1 error = {message, sizeof(message), 0};
    neo_jieba_engine_v1 *engine = NULL;
    if (neo_jieba_v1_engine_create(&paths, &engine, &error) != NEO_JIEBA_V1_OK) {
        fprintf(stderr, "%s\n", message);
        return 1;
    }

    neo_jieba_tokens_v1 *invalid = NULL;
    const neo_jieba_status_v1 failure =
        neo_jieba_v1_cut_utf8_owned(engine, "\xE4\xB8", 2, NEO_JIEBA_V1_MP, &invalid, &error);
    if (failure != NEO_JIEBA_V1_OPERATION_FAILED || invalid != NULL || error.message_size == 0) {
        neo_jieba_v1_tokens_destroy(invalid);
        neo_jieba_v1_engine_destroy(engine);
        return 2;
    }

    const char input[] = "中国科学院";
    neo_jieba_tokens_v1 *tokens = NULL;
    const neo_jieba_status_v1 status =
        neo_jieba_v1_cut_utf8_owned(engine, input, sizeof(input) - 1, NEO_JIEBA_V1_MIX, &tokens, &error);
    neo_jieba_v1_engine_destroy(engine);
    if (status != NEO_JIEBA_V1_OK) {
        fprintf(stderr, "%s\n", message);
        return 3;
    }

    neo_jieba_borrowed_tokens_v1 data = {0};
    const neo_jieba_status_v1 borrowed = neo_jieba_v1_tokens_borrow_data(tokens, &data, &error);
    const int valid = borrowed == NEO_JIEBA_V1_OK && data.utf8_size == sizeof(input) - 1 && data.count == 1 &&
                      data.ranges[0].byte_offset == 0 && data.ranges[0].byte_length == sizeof(input) - 1 &&
                      memcmp(data.utf8_data, input, sizeof(input) - 1) == 0;
    neo_jieba_v1_tokens_destroy(tokens);
    return valid ? 0 : 4;
}
