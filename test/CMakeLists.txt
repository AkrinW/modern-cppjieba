SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR})

# Configure test paths
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/test_paths.h.in" "${CMAKE_BINARY_DIR}/test/test_paths.h")

INCLUDE_DIRECTORIES(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/test
)

ADD_EXECUTABLE(load_test load_test.cpp)
ADD_EXECUTABLE(jieba_benchmark jieba_benchmark.cpp)
ADD_SUBDIRECTORY(unittest)
ADD_SUBDIRECTORY(unittest_neo)
ADD_EXECUTABLE(unicode_benchmark unicode_benchmark.cpp)
ADD_EXECUTABLE(prefilter_benchmark prefilter_benchmark.cpp)
ADD_EXECUTABLE(dict_trie_benchmark dict_trie_benchmark.cpp)
ADD_EXECUTABLE(dag_benchmark dag_benchmark.cpp)
ADD_EXECUTABLE(trie_benchmark trie_benchmark.cpp)
ADD_EXECUTABLE(query_segment_benchmark query_segment_benchmark.cpp)
