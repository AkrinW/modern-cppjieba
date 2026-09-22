set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/benchmark")

set(CPPJIEBA_BENCHMARK_TARGETS
    load_test
    jieba_benchmark
    unicode_benchmark
    prefilter_benchmark
    dict_trie_benchmark
    dag_benchmark
    trie_benchmark
    query_segment_benchmark
    fileio_benchmark
)
foreach(benchmark IN LISTS CPPJIEBA_BENCHMARK_TARGETS)
    add_executable(${benchmark} ${benchmark}.cpp)
    target_link_libraries(${benchmark} PRIVATE neo_cppjieba::neo_cppjieba cppjieba_test_data)
endforeach()

foreach(benchmark IN ITEMS load_test jieba_benchmark dict_trie_benchmark dag_benchmark trie_benchmark)
    target_link_libraries(${benchmark} PRIVATE cppjieba::cppjieba)
endforeach()
target_link_libraries(fileio_benchmark PRIVATE limonp::limonp)

if(CPPJIEBA_BUILD_RUST_BENCHMARKS)
    add_executable(cut_compare_benchmark cut_compare_benchmark.cpp)
    target_link_libraries(cut_compare_benchmark PRIVATE
        neo_cppjieba::neo_cppjieba cppjieba::cppjieba jieba_rs::jieba_rs cppjieba_test_data
    )
endif()
