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
    target_link_libraries(${benchmark} PRIVATE cppjieba cppjieba_test_data)
endforeach()

if(CPPJIEBA_BUILD_RUST_BENCHMARKS)
    find_program(CARGO_EXECUTABLE cargo HINTS "$ENV{HOME}/.cargo/bin" REQUIRED)
    set(RUST_BENCH_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/rust_benchmark/Cargo.toml")
    set(RUST_BENCH_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/rust-target")
    set(RUST_BENCH_LIB "${RUST_BENCH_TARGET_DIR}/release/${CMAKE_SHARED_LIBRARY_PREFIX}modern_cppjieba_rust_bench${CMAKE_SHARED_LIBRARY_SUFFIX}")
    add_custom_target(jieba_rs_capi_build
        COMMAND "${CARGO_EXECUTABLE}" build --release --locked
                --manifest-path "${RUST_BENCH_MANIFEST}" --target-dir "${RUST_BENCH_TARGET_DIR}"
        BYPRODUCTS "${RUST_BENCH_LIB}"
        COMMENT "Building the pinned jieba-rs benchmark adapter (release)"
        VERBATIM
    )
    add_executable(cut_compare_benchmark cut_compare_benchmark.cpp)
    add_dependencies(cut_compare_benchmark jieba_rs_capi_build)
    target_link_libraries(cut_compare_benchmark PRIVATE cppjieba cppjieba_test_data "${RUST_BENCH_LIB}")
    if(NOT WIN32)
        set_target_properties(cut_compare_benchmark PROPERTIES BUILD_RPATH "${RUST_BENCH_TARGET_DIR}/release")
    endif()
endif()
