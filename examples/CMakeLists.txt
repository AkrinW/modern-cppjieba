add_executable(jieba_basic_cut basic_cut.cpp)
target_link_libraries(jieba_basic_cut PRIVATE neo_cppjieba::neo_cppjieba)

add_executable(jieba_batch_cut batch_cut.cpp)
target_link_libraries(jieba_batch_cut PRIVATE neo_cppjieba::neo_cppjieba)

add_executable(jieba_search_highlight search_highlight.cpp)
target_link_libraries(jieba_search_highlight PRIVATE neo_cppjieba::neo_cppjieba)

add_executable(jieba_word_count word_count.cpp)
target_link_libraries(jieba_word_count PRIVATE neo_cppjieba::neo_cppjieba)

add_executable(jieba_owned_results owned_results.cpp)
target_link_libraries(jieba_owned_results PRIVATE neo_cppjieba::neo_cppjieba)

if(MSVC)
    # Keep main(argv) and filesystem narrow paths in UTF-8 on Windows 10 1903 and later.
    foreach(example IN ITEMS jieba_basic_cut jieba_batch_cut jieba_search_highlight jieba_word_count jieba_owned_results)
        target_sources(${example} PRIVATE "${PROJECT_SOURCE_DIR}/cmake/utf8.manifest")
    endforeach()
endif()

if(BUILD_TESTING)
    add_test(NAME example_basic_cut COMMAND jieba_basic_cut "${PROJECT_SOURCE_DIR}/dict" "我爱北京天安门")
    set_tests_properties(example_basic_cut PROPERTIES PASS_REGULAR_EXPRESSION "MIX: \\[我\\] \\[爱\\] \\[北京\\] \\[天安门\\]")
endif()
