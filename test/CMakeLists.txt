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

option(CPPJIEBA_BUILD_RUST_BENCHMARKS "Build the pinned Rust comparison benchmark (requires Cargo)" OFF)
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
    target_link_libraries(cut_compare_benchmark PRIVATE "${RUST_BENCH_LIB}")
    if(NOT WIN32)
        set_target_properties(cut_compare_benchmark PROPERTIES
            BUILD_RPATH "${RUST_BENCH_TARGET_DIR}/release"
        )
    endif()
endif()
