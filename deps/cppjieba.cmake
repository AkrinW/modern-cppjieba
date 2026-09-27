if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cppjieba/include/cppjieba/Jieba.hpp")
    message(FATAL_ERROR "Legacy comparisons require cppjieba. Run: git submodule update --init deps/cppjieba")
endif()

add_library(cppjieba_legacy INTERFACE)
add_library(cppjieba::cppjieba ALIAS cppjieba_legacy)
target_include_directories(cppjieba_legacy SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/cppjieba/include")
