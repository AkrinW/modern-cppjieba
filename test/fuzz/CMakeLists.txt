# libFuzzer ships with Clang; AppleClang omits it.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "CPPJIEBA_BUILD_FUZZERS requires Clang with libFuzzer")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/fuzz")

# Every target runs with ASan and UBSan; build without NDEBUG to keep library assertions fatal.
function(cppjieba_add_fuzzer name)
    set(sanitizers -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined)
    add_executable(${name} ${ARGN})
    target_compile_options(${name} PRIVATE ${sanitizers})
    target_link_options(${name} PRIVATE ${sanitizers})
    target_link_libraries(${name} PRIVATE neo_cppjieba::neo_cppjieba)
endfunction()

cppjieba_add_fuzzer(utf_decode_fuzzer utf_decode_fuzzer.cpp)
cppjieba_add_fuzzer(main_dict_fuzzer main_dict_fuzzer.cpp)
cppjieba_add_fuzzer(user_dict_fuzzer user_dict_fuzzer.cpp)
cppjieba_add_fuzzer(hmm_model_fuzzer hmm_model_fuzzer.cpp)
cppjieba_add_fuzzer(dict_snapshot_fuzzer dict_snapshot_fuzzer.cpp)

# Compile the adapter into the fuzzer so it is instrumented without building the C ABI libraries.
cppjieba_add_fuzzer(c_api_cut_fuzzer c_api_cut_fuzzer.cpp "${PROJECT_SOURCE_DIR}/api/src/neo_cppjieba.cpp")
target_include_directories(c_api_cut_fuzzer PRIVATE "${PROJECT_SOURCE_DIR}/api/include")
target_compile_definitions(c_api_cut_fuzzer PRIVATE NEO_CPPJIEBA_C_STATIC)
target_link_libraries(c_api_cut_fuzzer PRIVATE cppjieba_test_data)
