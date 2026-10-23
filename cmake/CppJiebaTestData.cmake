# Configure test paths
configure_file("${PROJECT_SOURCE_DIR}/test/test_paths.h.in" "${PROJECT_BINARY_DIR}/generated/test_paths.h" @ONLY)
add_library(cppjieba_test_data INTERFACE)
target_include_directories(cppjieba_test_data INTERFACE
    "${PROJECT_BINARY_DIR}/generated"
    "${PROJECT_SOURCE_DIR}/test"
)
