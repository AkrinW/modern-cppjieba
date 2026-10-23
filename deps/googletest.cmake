if(TARGET GTest::gtest)
    return()
endif()

find_package(GTest QUIET)
if(GTest_FOUND)
    return()
endif()

if(MSVC)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
)
FetchContent_MakeAvailable(googletest)
