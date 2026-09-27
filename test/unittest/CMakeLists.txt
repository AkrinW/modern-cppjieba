set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test")

ADD_EXECUTABLE(test.run
    ../gtest_main.cpp
    keyword_extractor_test.cpp
    trie_test.cpp
    segments_test.cpp
    pos_tagger_test.cpp
    jieba_test.cpp
    pre_filter_test.cpp
    unicode_test.cpp
    textrank_test.cpp
)

target_compile_definitions(test.run PRIVATE LOGGING_LEVEL=LL_WARNING)
# Add include directories
target_link_libraries(test.run PRIVATE cppjieba::cppjieba cppjieba_test_data GTest::gtest)
add_test(NAME legacy_unit_tests COMMAND test.run)
