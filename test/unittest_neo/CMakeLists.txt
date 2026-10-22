set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test")

ADD_EXECUTABLE(test_neo.run
    ../gtest_main.cpp
    unicode_test.cpp
    traits_test.cpp
    logging_test.cpp
    fileio_test.cpp
    benchmark_utils_test.cpp
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
if(CPPJIEBA_ENABLE_ICU)
    target_sources(test_neo.run PRIVATE encoded_text_test.cpp)
    target_link_libraries(test_neo.run PRIVATE neo_cppjieba::icu)
endif()
add_test(NAME neo_unit_tests COMMAND test_neo.run)

if(CPPJIEBA_BUILD_C_API)
    add_executable(test_c_api.run ../gtest_main.cpp c_api_test.cpp)
    target_link_libraries(test_c_api.run PRIVATE
        neo_cppjieba::c neo_cppjieba::neo_cppjieba cppjieba_test_data GTest::gtest)
    add_test(NAME neo_c_api_tests COMMAND test_c_api.run)

    add_executable(test_c_api_smoke.run c_api_smoke.c)
    set_target_properties(test_c_api_smoke.run PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    target_link_libraries(test_c_api_smoke.run PRIVATE neo_cppjieba::c cppjieba_test_data)
    target_link_options(test_c_api_smoke.run PRIVATE "LINKER:--export-dynamic")
    add_test(NAME neo_c_api_c_host COMMAND test_c_api_smoke.run)

    find_program(NEO_C_API_NM NAMES llvm-nm nm REQUIRED)
    find_program(NEO_C_API_READELF NAMES llvm-readelf readelf REQUIRED)
    add_test(NAME neo_c_api_symbols COMMAND ${CMAKE_COMMAND}
        "-DLIBRARY=$<TARGET_FILE:neo_cppjieba_c>"
        "-DNM=${NEO_C_API_NM}"
        "-DREADELF=${NEO_C_API_READELF}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_c_api_symbols.cmake")
endif()

# Each capacity variant is a separate executable so inline definitions never mix configurations.
set(capacity_config_path "${PROJECT_SOURCE_DIR}/include/neo/Config.hpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${capacity_config_path}")
file(READ "${capacity_config_path}" capacity_config_source)

function(add_capacity_test name rune_type source_type dag_type node_type test_source)
    set(config "${capacity_config_source}")
    # Capacity and existing regression fixtures retain the CPP output contract.
    string(REGEX REPLACE "segmentation_style = SegmentationStyle::(CPP|RUST|PYTHON);"
        "segmentation_style = SegmentationStyle::CPP;" config "${config}")
    string(REGEX REPLACE "using RuneIndex = [^;]+;" "using RuneIndex = ${rune_type};" config "${config}")
    string(REGEX REPLACE "using SourceOffset = [^;]+;" "using SourceOffset = ${source_type};" config "${config}")
    string(REGEX REPLACE "using DagOffset = [^;]+;" "using DagOffset = ${dag_type};" config "${config}")
    string(REGEX REPLACE "using TrieNodeId = [^;]+;" "using TrieNodeId = ${node_type};" config "${config}")
    set(config_dir "${PROJECT_BINARY_DIR}/generated/capacity/${name}")
    file(WRITE "${config_dir}/neo/Config.hpp" "${config}")

    set(target "test_capacity_${name}.run")
    add_executable(${target} ../gtest_main.cpp ${test_source})
    target_include_directories(${target} BEFORE PRIVATE "${config_dir}")
    target_link_libraries(${target} PRIVATE neo_cppjieba::neo_cppjieba cppjieba_test_data GTest::gtest)
    add_test(NAME neo_capacity_${name} COMMAND ${target})
endfunction()

# Each style has its own executable and generated header, avoiding mixed inline definitions.
foreach(style IN ITEMS CPP RUST PYTHON)
    string(TOLOWER "${style}" style_name)
    set(config "${capacity_config_source}")
    string(REGEX REPLACE "segmentation_style = SegmentationStyle::(CPP|RUST|PYTHON);"
        "segmentation_style = SegmentationStyle::${style};" config "${config}")
    set(config_dir "${PROJECT_BINARY_DIR}/generated/style/${style_name}")
    file(WRITE "${config_dir}/neo/Config.hpp" "${config}")
    set(target "test_style_${style_name}.run")
    add_executable(${target} ../gtest_main.cpp segmentation_style_test.cpp)
    target_include_directories(${target} BEFORE PRIVATE "${config_dir}")
    target_link_libraries(${target} PRIVATE neo_cppjieba::neo_cppjieba cppjieba_test_data GTest::gtest)
    add_test(NAME neo_style_${style_name} COMMAND ${target})
endforeach()

foreach(bits IN ITEMS 8 16 32 64)
    add_capacity_test(${bits} "std::uint${bits}_t" "std::uint${bits}_t" "std::uint${bits}_t"
        "std::uint32_t" capacity_test.cpp)
    add_capacity_test(trie_${bits} "std::uint32_t" "std::uint32_t" "std::uint32_t"
        "std::uint${bits}_t" trie_capacity_test.cpp)
endforeach()
# Existing regression fixtures require more capacity than the narrow variants provide.
target_include_directories(test_neo.run BEFORE PRIVATE "${PROJECT_BINARY_DIR}/generated/capacity/32")
add_capacity_test(mixed "std::uint8_t" "std::uint16_t" "std::uint32_t" "std::uint64_t" capacity_test.cpp)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles("int main() { unsigned __int128 value = 0; return static_cast<int>(value); }"
    NEO_TEST_HAS_UINT128)
if(NEO_TEST_HAS_UINT128)
    add_capacity_test(128 "uint128_t" "uint128_t" "uint128_t" "uint128_t" capacity_test.cpp)
    add_capacity_test(mixed128 "uint128_t" "std::uint8_t" "std::uint16_t" "std::uint64_t" capacity_test.cpp)
    add_capacity_test(trie_128 "std::uint32_t" "std::uint32_t" "std::uint32_t" "uint128_t" trie_capacity_test.cpp)
endif()
