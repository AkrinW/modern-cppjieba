cmake_minimum_required(VERSION 3.20)
project(modern_cppjieba LANGUAGES CXX)

if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Define a variable to check if this is the top-level project
if(NOT DEFINED CPPJIEBA_TOP_LEVEL_PROJECT)
    if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        set(CPPJIEBA_TOP_LEVEL_PROJECT ON)
    else()
        set(CPPJIEBA_TOP_LEVEL_PROJECT OFF)
    endif()
endif()

include(GNUInstallDirs)
add_library(neo_cppjieba INTERFACE)
add_library(neo_cppjieba::neo_cppjieba ALIAS neo_cppjieba)
target_compile_features(neo_cppjieba INTERFACE cxx_std_23)
target_include_directories(neo_cppjieba INTERFACE
    "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

install(DIRECTORY include/neo
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY dict/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/cppjieba/dict)

option(BUILD_TESTING "Build unit tests" ${CPPJIEBA_TOP_LEVEL_PROJECT})
option(CPPJIEBA_BUILD_LEGACY_TESTS "Build tests for the legacy dependency" OFF)
option(CPPJIEBA_BUILD_BENCHMARKS "Build standalone benchmarks" OFF)
option(CPPJIEBA_BUILD_RUST_BENCHMARKS "Build the pinned Rust comparison benchmark (requires Cargo)" OFF)

if(CPPJIEBA_BUILD_LEGACY_TESTS AND NOT BUILD_TESTING)
    message(FATAL_ERROR "CPPJIEBA_BUILD_LEGACY_TESTS requires BUILD_TESTING=ON")
endif()
if(CPPJIEBA_BUILD_RUST_BENCHMARKS AND NOT CPPJIEBA_BUILD_BENCHMARKS)
    message(FATAL_ERROR "CPPJIEBA_BUILD_RUST_BENCHMARKS requires CPPJIEBA_BUILD_BENCHMARKS=ON")
endif()

if(CPPJIEBA_BUILD_LEGACY_TESTS OR CPPJIEBA_BUILD_BENCHMARKS)
    add_subdirectory(deps)
endif()

if(BUILD_TESTING OR CPPJIEBA_BUILD_BENCHMARKS)
    # Configure test paths
    configure_file(test/test_paths.h.in "${PROJECT_BINARY_DIR}/generated/test_paths.h")
    add_library(cppjieba_test_data INTERFACE)
    target_include_directories(cppjieba_test_data INTERFACE
        "${PROJECT_BINARY_DIR}/generated"
        "${PROJECT_SOURCE_DIR}/test"
    )
endif()

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()
if(CPPJIEBA_BUILD_BENCHMARKS)
    add_subdirectory(benchmark)
endif()
