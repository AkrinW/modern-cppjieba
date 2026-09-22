set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test")

ADD_EXECUTABLE(test_neo.run
    ../gtest_main.cpp
    unicode_test.cpp
    logging_test.cpp
    fileio_test.cpp
    postag_test.cpp
    trie_stats_test.cpp
    dicttrie_test.cpp
    segment_api_test.cpp
    hmmseg_test.cpp
    fullseg_test.cpp
    mpseg_test.cpp
    mixseg_test.cpp
    queryseg_test.cpp
    jieba_test.cpp
)

target_compile_definitions(test_neo.run PRIVATE LOGGING_LEVEL=LL_WARNING)
# Add include directories
target_link_libraries(test_neo.run PRIVATE neo_cppjieba::neo_cppjieba cppjieba_test_data GTest::gtest)
add_test(NAME neo_unit_tests COMMAND test_neo.run)
