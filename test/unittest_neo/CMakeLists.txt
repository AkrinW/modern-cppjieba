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
    # Exercise the same ownership and error contract through each enabled library variant.
    function(add_c_api_tests name library)
        add_executable(test_${name}.run ../gtest_main.cpp c_api_test.cpp)
        target_link_libraries(test_${name}.run PRIVATE
            ${library} neo_cppjieba::neo_cppjieba cppjieba_test_data GTest::gtest)
        add_test(NAME neo_${name}_tests COMMAND test_${name}.run)

        add_executable(test_${name}_smoke.run c_api_smoke.c)
        set_target_properties(test_${name}_smoke.run PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
        target_compile_options(test_${name}_smoke.run PRIVATE "$<$<C_COMPILER_ID:MSVC>:/utf-8>")
        target_link_libraries(test_${name}_smoke.run PRIVATE ${library} cppjieba_test_data)
        add_test(NAME neo_${name}_c_host COMMAND test_${name}_smoke.run)
    endfunction()

    if(CPPJIEBA_C_API_BUILD_SHARED)
        add_c_api_tests(c_api neo_cppjieba::c_shared)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_compile_definitions(test_c_api_smoke.run PRIVATE NEO_CPPJIEBA_C_TEST_PRIVATE_RUNTIME)
            target_link_options(test_c_api_smoke.run PRIVATE "LINKER:--export-dynamic")
            find_program(NEO_C_API_NM NAMES llvm-nm nm REQUIRED)
            find_program(NEO_C_API_READELF NAMES llvm-readelf readelf REQUIRED)
        elseif(APPLE)
            find_program(NEO_C_API_NM NAMES llvm-nm nm REQUIRED)
            find_program(NEO_C_API_OTOOL NAMES llvm-otool otool REQUIRED)
        elseif(MSVC)
            get_filename_component(c_api_msvc_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
            find_program(NEO_C_API_DUMPBIN NAMES dumpbin HINTS "${c_api_msvc_dir}" REQUIRED)
        endif()
        add_test(NAME neo_c_api_symbols COMMAND ${CMAKE_COMMAND}
            "-DLIBRARY=$<TARGET_FILE:neo_cppjieba_c>"
            "-DPLATFORM=${CMAKE_SYSTEM_NAME}"
            "-DNM=${NEO_C_API_NM}" "-DREADELF=${NEO_C_API_READELF}"
            "-DOTOOL=${NEO_C_API_OTOOL}" "-DDUMPBIN=${NEO_C_API_DUMPBIN}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/check_c_api_symbols.cmake")
    endif()
    if(CPPJIEBA_C_API_BUILD_STATIC)
        add_c_api_tests(c_api_static neo_cppjieba::c_static)
    endif()

    add_test(NAME neo_c_api_install COMMAND ${CMAKE_COMMAND}
        "-DBUILD_DIR=${PROJECT_BINARY_DIR}" "-DINSTALL_LIBDIR=${CMAKE_INSTALL_LIBDIR}"
        "-DCONSUMER_DIR=${PROJECT_SOURCE_DIR}/test/c_api_consumer" "-DCONFIG=$<CONFIG>"
        "-DBUILD_SHARED=${CPPJIEBA_C_API_BUILD_SHARED}" "-DBUILD_STATIC=${CPPJIEBA_C_API_BUILD_STATIC}"
        "-DGENERATOR=${CMAKE_GENERATOR}" "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
        "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
        "-DC_COMPILER=${CMAKE_C_COMPILER}" "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
        "-DCXX_FLAGS=${CMAKE_CXX_FLAGS}" "-DEXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
        "-DMSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}"
        "-DOSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}" "-DOSX_SYSROOT=${CMAKE_OSX_SYSROOT}"
        "-DOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
        "-DCTEST=${CMAKE_CTEST_COMMAND}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/check_c_api_install.cmake")
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
