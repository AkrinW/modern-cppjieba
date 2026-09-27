if(MSVC)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_compile_options(/utf-8)
endif()

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
)
FetchContent_MakeAvailable(googletest)

add_subdirectory(unittest_neo)
if(CPPJIEBA_BUILD_LEGACY_TESTS)
    add_subdirectory(unittest)
endif()
