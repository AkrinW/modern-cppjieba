SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR})

# Configure test paths
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/test_paths.h.in" "${CMAKE_BINARY_DIR}/test/test_paths.h")

INCLUDE_DIRECTORIES(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/test
)

find_program(CARGO_EXECUTABLE cargo HINTS "$ENV{HOME}/.cargo/bin")

if(WIN32)
    set(JIEBA_RS_CAPI_BENCH_LIB "${CMAKE_SOURCE_DIR}/deps/jieba-rs/target/release/jieba_capi.dll")
elseif(APPLE)
    set(JIEBA_RS_CAPI_BENCH_LIB "${CMAKE_SOURCE_DIR}/deps/jieba-rs/target/release/libjieba_capi.dylib")
else()
    set(JIEBA_RS_CAPI_BENCH_LIB "${CMAKE_SOURCE_DIR}/deps/jieba-rs/target/release/libjieba_capi.so")
endif()

if(CARGO_EXECUTABLE)
    add_custom_command(
        OUTPUT "${JIEBA_RS_CAPI_BENCH_LIB}"
        COMMAND "${CARGO_EXECUTABLE}" build --release -p jieba-capi
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/deps/jieba-rs"
        COMMENT "Building jieba-rs C API benchmark library"
        VERBATIM
    )
    add_custom_target(jieba_rs_capi_build DEPENDS "${JIEBA_RS_CAPI_BENCH_LIB}")
endif()

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

if(CARGO_EXECUTABLE OR EXISTS "${JIEBA_RS_CAPI_BENCH_LIB}")
    add_executable(cut_compare_benchmark cut_compare_benchmark.cpp)
    target_link_libraries(cut_compare_benchmark PRIVATE "${JIEBA_RS_CAPI_BENCH_LIB}")
    if(NOT WIN32)
        get_filename_component(JIEBA_RS_CAPI_BENCH_LIB_DIR "${JIEBA_RS_CAPI_BENCH_LIB}" DIRECTORY)
        set_target_properties(cut_compare_benchmark PROPERTIES
            BUILD_RPATH "${JIEBA_RS_CAPI_BENCH_LIB_DIR}"
            INSTALL_RPATH "${JIEBA_RS_CAPI_BENCH_LIB_DIR}"
        )
    endif()
    if(TARGET jieba_rs_capi_build)
        add_dependencies(cut_compare_benchmark jieba_rs_capi_build)
    endif()
else()
    message(WARNING "Rust benchmark target 'cut_compare_benchmark' is disabled: cargo not found and ${JIEBA_RS_CAPI_BENCH_LIB} is missing.")
endif()
