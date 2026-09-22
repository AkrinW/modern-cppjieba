add_library(cppjieba_legacy INTERFACE)
add_library(cppjieba::cppjieba ALIAS cppjieba_legacy)
target_include_directories(cppjieba_legacy SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(cppjieba_legacy INTERFACE limonp::limonp)
