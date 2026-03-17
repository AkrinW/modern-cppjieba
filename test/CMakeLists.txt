SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR})

# Configure test paths
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/test_paths.h.in" "${CMAKE_BINARY_DIR}/test/test_paths.h")

INCLUDE_DIRECTORIES(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/test
)

ADD_EXECUTABLE(load_test load_test.cpp)
ADD_SUBDIRECTORY(unittest)
ADD_SUBDIRECTORY(unittest_neo)
ADD_EXECUTABLE(unicode_benchmark unicode_benchmark.cpp)
ADD_EXECUTABLE(prefilter_benchmark prefilter_benchmark.cpp)
