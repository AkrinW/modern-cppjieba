if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
    message(FATAL_ERROR "The C ABI library currently requires Linux/ELF and GCC or Clang with the GNU C++ runtime")
endif()

add_library(neo_cppjieba_c SHARED src/neo_cppjieba.cpp)
add_library(neo_cppjieba::c ALIAS neo_cppjieba_c)
target_link_libraries(neo_cppjieba_c PRIVATE neo_cppjieba::neo_cppjieba)
target_include_directories(neo_cppjieba_c PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)
set_target_properties(neo_cppjieba_c PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(neo_cppjieba_c PRIVATE -fno-gnu-unique)
endif()

# Both implementation and bundled runtime definitions stay local to this shared object.
# Runtime archives must support PIC; no C++ exception or allocation ownership crosses the ABI.
target_link_options(neo_cppjieba_c PRIVATE
    -static-libstdc++ -static-libgcc
    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map"
    "LINKER:--exclude-libs,ALL"
    "LINKER:--gc-sections"
    "LINKER:--undefined=__cxa_pure_virtual"
    "LINKER:-z,defs"
    "LINKER:-z,text"
)
# Discard unused transactional-runtime sections, and resolve weak pure-virtual references privately.
set_property(TARGET neo_cppjieba_c APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.map")

install(TARGETS neo_cppjieba_c LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/neo_cppjieba.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
