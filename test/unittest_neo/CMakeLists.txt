message(STATUS "MSVC value: ${MSVC}")
if (MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDebugDLL")
	set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
	add_compile_options(/utf-8)
endif()

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/test)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

ADD_DEFINITIONS(-DLOGGING_LEVEL=LL_WARNING)

# Add include directories
INCLUDE_DIRECTORIES(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_BINARY_DIR}/test
)

ADD_EXECUTABLE(test_neo.run
    ${CMAKE_SOURCE_DIR}/test/unittest/gtest_main.cpp
    unicode_test.cpp
    logging_test.cpp
    fileio_test.cpp
    postag_test.cpp
    trie_stats_test.cpp
    dicttrie_test.cpp
    hmmseg_test.cpp
    fullseg_test.cpp
    mpseg_test.cpp
    mixseg_test.cpp
)

TARGET_LINK_LIBRARIES(test_neo.run gtest)
