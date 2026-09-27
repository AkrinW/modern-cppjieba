if(CPPJIEBA_BUILD_BENCHMARKS)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/limonp/include/limonp/StringUtil.hpp")
        message(FATAL_ERROR "Comparison helpers require limonp. Run: git submodule update --init deps/limonp")
    endif()
    # The upstream project unconditionally enables its own tests; expose only its headers here.
    add_library(cppjieba_limonp INTERFACE)
    add_library(limonp::limonp ALIAS cppjieba_limonp)
    target_include_directories(cppjieba_limonp SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/limonp/include")
endif()

include(cppjieba.cmake)
if(CPPJIEBA_BUILD_RUST_BENCHMARKS)
    add_subdirectory(rust-jieba)
endif()
