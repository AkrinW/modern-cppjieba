CMAKE_MINIMUM_REQUIRED (VERSION 3.20)
PROJECT(CPPJIEBA)

find_package(limonp)
if(limonp_FOUND)
    get_target_property(LIMONP_INCLUDE_DIR limonp::limonp INTERFACE_INCLUDE_DIRECTORIES)
else()
    set(LIMONP_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/deps/limonp/include")
endif()
INCLUDE_DIRECTORIES("${LIMONP_INCLUDE_DIR}"
  ${PROJECT_SOURCE_DIR}/include)

if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 23)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

ADD_DEFINITIONS(-O3 -g)

# Define a variable to check if this is the top-level project
if(NOT DEFINED CPPJIEBA_TOP_LEVEL_PROJECT)
    if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        set(CPPJIEBA_TOP_LEVEL_PROJECT ON)
    else()
        set(CPPJIEBA_TOP_LEVEL_PROJECT OFF)
    endif()
endif()

if(NOT TARGET cppjieba)
  add_library(cppjieba INTERFACE)
  target_include_directories(cppjieba INTERFACE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/deps/limonp/include
  )
endif()

include(GNUInstallDirs)
install(DIRECTORY include/
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
