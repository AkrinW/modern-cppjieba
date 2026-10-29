# Compile every library header alone, twice, so no header relies on declarations its includer brought in first.
# The set follows the installed include/neo tree, so new headers are checked without being listed here;
# vendored third-party headers keep their upstream layout, and encoding/ needs the ICU target.
file(GLOB_RECURSE neo_headers CONFIGURE_DEPENDS RELATIVE "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include/neo/*.hpp")
list(FILTER neo_headers EXCLUDE REGEX "^neo/third_party/")
if(NOT CPPJIEBA_ENABLE_ICU)
    list(FILTER neo_headers EXCLUDE REGEX "^neo/encoding/")
endif()

set(header_check_sources)
foreach(header IN LISTS neo_headers)
    string(MAKE_C_IDENTIFIER "${header}" name)
    set(source "${PROJECT_BINARY_DIR}/generated/header_check/${name}.cpp")
    # file(CONFIGURE) rewrites the source only when its content changes.
    file(CONFIGURE OUTPUT "${source}" CONTENT "#include \"${header}\"\n#include \"${header}\"\n")
    list(APPEND header_check_sources "${source}")
endforeach()

add_library(neo_header_check OBJECT ${header_check_sources})
target_link_libraries(neo_header_check PRIVATE neo_cppjieba::neo_cppjieba)
if(CPPJIEBA_ENABLE_ICU)
    target_link_libraries(neo_header_check PRIVATE neo_cppjieba::icu)
endif()
