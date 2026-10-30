#include "neo_cppjieba.h"
#include "test_paths.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(neo_jieba_status_v1) == 4, "Status ABI must remain 32-bit");
_Static_assert(sizeof(neo_jieba_mode_v1) == 4, "Mode ABI must remain 32-bit");
_Static_assert(sizeof(neo_jieba_token_range_v1) == 16, "Token ABI must remain two uint64_t fields");
_Static_assert(offsetof(neo_jieba_token_range_v1, byte_length) == 8, "Token field offset changed");
_Static_assert(sizeof(neo_jieba_source_kind_v1) == 4, "Source kind ABI must remain 32-bit");
_Static_assert(offsetof(neo_jieba_source_v1, data) == 8, "Source data must follow two 32-bit fields");
_Static_assert(sizeof(neo_jieba_pos_tag_v1) == 4, "POS tags must remain four bytes");
_Static_assert(sizeof(neo_jieba_keyword_v1) == 32, "Keyword records must remain 32 bytes");
_Static_assert(offsetof(neo_jieba_keyword_v1, tag) == 24, "Keyword tag offset changed");

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

/* Reuse caller-owned handles and check that the ranges index the caller's input. */
static int ranges_match(const neo_jieba_engine_v1 *engine, const char *input, uint64_t size) {
    neo_jieba_workspace_v1 *workspace = NULL;
    neo_jieba_ranges_v1 *ranges = NULL;
    neo_jieba_borrowed_ranges_v1 data = {0};
    const int valid =
        neo_jieba_v1_workspace_create(&workspace, NULL) == NEO_JIEBA_V1_OK &&
        neo_jieba_v1_ranges_create(&ranges, NULL) == NEO_JIEBA_V1_OK &&
        neo_jieba_v1_cut_utf8_into(engine, input, size, NEO_JIEBA_V1_MIX, workspace, ranges, NULL) == NEO_JIEBA_V1_OK &&
        neo_jieba_v1_ranges_borrow_data(ranges, &data, NULL) == NEO_JIEBA_V1_OK && data.count == 1 &&
        data.ranges[0].byte_offset == 0 && data.ranges[0].byte_length == size;
    neo_jieba_v1_ranges_destroy(ranges);
    neo_jieba_v1_workspace_destroy(workspace);
    return valid;
}

/* A tagged fill attaches the same nonempty tag that lookup_tag reports for the whole word. */
static int tags_match(const neo_jieba_engine_v1 *engine, const char *input, uint64_t size) {
    neo_jieba_workspace_v1 *workspace = NULL;
    neo_jieba_ranges_v1 *ranges = NULL;
    neo_jieba_borrowed_ranges_v1 data = {0};
    neo_jieba_pos_tag_v1 tag = {{0}};
    const int valid = neo_jieba_v1_workspace_create(&workspace, NULL) == NEO_JIEBA_V1_OK &&
                      neo_jieba_v1_ranges_create(&ranges, NULL) == NEO_JIEBA_V1_OK &&
                      neo_jieba_v1_cut_utf8_tagged_into(engine, input, size, NEO_JIEBA_V1_MIX, workspace, ranges,
                                                        NULL) == NEO_JIEBA_V1_OK &&
                      neo_jieba_v1_ranges_borrow_data(ranges, &data, NULL) == NEO_JIEBA_V1_OK && data.count == 1 &&
                      data.tags != NULL &&
                      neo_jieba_v1_lookup_tag(engine, input, size, &tag, NULL) == NEO_JIEBA_V1_OK &&
                      tag.data[0] != '\0' && memcmp(data.tags[0].data, tag.data, sizeof(tag.data)) == 0;
    neo_jieba_v1_ranges_destroy(ranges);
    neo_jieba_v1_workspace_destroy(workspace);
    return valid;
}

/* Edit a private clone: an added word reads back with its tag, and removal reports presence only once. */
static int edits_work(const neo_jieba_engine_v1 *engine) {
    static const char word[] = "杭研大厦";
    const neo_jieba_pos_tag_v1 tag = {{'n', 'z', 0, 0}};
    neo_jieba_pos_tag_v1 found = {{0}};
    uint32_t removed = 0;
    uint32_t removed_again = 1;
    neo_jieba_engine_v1 *clone = NULL;
    const int valid =
        neo_jieba_v1_engine_clone(engine, &clone, NULL) == NEO_JIEBA_V1_OK &&
        neo_jieba_v1_engine_add_word(clone, word, sizeof(word) - 1, &tag, NULL) == NEO_JIEBA_V1_OK &&
        neo_jieba_v1_lookup_tag(clone, word, sizeof(word) - 1, &found, NULL) == NEO_JIEBA_V1_OK &&
        memcmp(found.data, tag.data, sizeof(tag.data)) == 0 &&
        neo_jieba_v1_engine_remove_word(clone, word, sizeof(word) - 1, &removed, NULL) == NEO_JIEBA_V1_OK &&
        removed == 1 &&
        neo_jieba_v1_engine_remove_word(clone, word, sizeof(word) - 1, &removed_again, NULL) == NEO_JIEBA_V1_OK &&
        removed_again == 0 && neo_jieba_v1_engine_compact(clone, NULL) == NEO_JIEBA_V1_OK;
    neo_jieba_v1_engine_destroy(clone);
    return valid;
}

/* Every keyword's first occurrence must be a nonempty range inside the input. */
static int keywords_in_input(const neo_jieba_borrowed_keywords_v1 *data, uint64_t input_size) {
    for (uint64_t index = 0; index < data->count; ++index) {
        const neo_jieba_keyword_v1 *keyword = &data->keywords[index];
        if (keyword->occurrence_count == 0 || keyword->first_occurrence >= data->occurrence_count) {
            return 0;
        }
        const neo_jieba_token_range_v1 *first = &data->occurrences[keyword->first_occurrence];
        if (first->byte_length == 0 || first->byte_offset + first->byte_length > input_size) {
            return 0;
        }
    }
    return 1;
}

/* Extract TF-IDF keywords in C through caller-owned handles. */
static int keywords_work(const neo_jieba_engine_v1 *engine) {
    static const char idf_path[] = DICT_DIR "/idf.utf8";
    static const char stop_words_path[] = DICT_DIR "/stop_words.utf8";
    static const char input[] = "我是拖拉机学院手扶拖拉机专业的。不用多久，我就会升职加薪，当上CEO，走上人生巅峰。";
    const neo_jieba_source_v1 idf = {NEO_JIEBA_V1_SOURCE_FILE, 0, idf_path, sizeof(idf_path) - 1};
    const neo_jieba_source_v1 stop_words = {NEO_JIEBA_V1_SOURCE_FILE, 0, stop_words_path, sizeof(stop_words_path) - 1};
    const neo_jieba_tfidf_options_v1 options = {NULL, 0};
    neo_jieba_tfidf_v1 *extractor = NULL;
    neo_jieba_workspace_v1 *workspace = NULL;
    neo_jieba_keywords_v1 *keywords = NULL;
    neo_jieba_borrowed_keywords_v1 data = {0};
    const int valid = neo_jieba_v1_tfidf_create(&idf, &stop_words, &extractor, NULL) == NEO_JIEBA_V1_OK &&
                      neo_jieba_v1_workspace_create(&workspace, NULL) == NEO_JIEBA_V1_OK &&
                      neo_jieba_v1_keywords_create(&keywords, NULL) == NEO_JIEBA_V1_OK &&
                      neo_jieba_v1_tfidf_extract_into(extractor, engine, input, sizeof(input) - 1, 3, &options,
                                                      workspace, keywords, NULL) == NEO_JIEBA_V1_OK &&
                      neo_jieba_v1_keywords_borrow_data(keywords, &data, NULL) == NEO_JIEBA_V1_OK && data.count == 3 &&
                      keywords_in_input(&data, sizeof(input) - 1);
    neo_jieba_v1_keywords_destroy(keywords);
    neo_jieba_v1_workspace_destroy(workspace);
    neo_jieba_v1_tfidf_destroy(extractor);
    return valid;
}

/* Create an engine from explicit-length file sources, as callers without NUL-terminated strings do. */
static int sources_engine_works(void) {
    static const char dict_path[] = DICT_DIR "/jieba.dict.utf8";
    static const char model_path[] = DICT_DIR "/hmm_model.utf8";
    const neo_jieba_source_v1 dict = {NEO_JIEBA_V1_SOURCE_FILE, 0, dict_path, sizeof(dict_path) - 1};
    const neo_jieba_source_v1 model = {NEO_JIEBA_V1_SOURCE_FILE, 0, model_path, sizeof(model_path) - 1};
    neo_jieba_engine_v1 *engine = NULL;
    const int valid = neo_jieba_v1_engine_create_from_sources(&dict, &model, NULL, 0, &engine, NULL) == NEO_JIEBA_V1_OK;
    neo_jieba_v1_engine_destroy(engine);
    return valid;
}

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
    const int ranges_valid = ranges_match(engine, input, sizeof(input) - 1);
    const int tags_valid = tags_match(engine, input, sizeof(input) - 1);
    const int edits_valid = edits_work(engine);
    const int keywords_valid = keywords_work(engine);
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
    if (!valid) {
        return 4;
    }
    if (!ranges_valid) {
        return 5;
    }
    if (!tags_valid) {
        return 7;
    }
    if (!edits_valid) {
        return 8;
    }
    if (!keywords_valid) {
        return 9;
    }
    return sources_engine_works() ? 0 : 6;
}
