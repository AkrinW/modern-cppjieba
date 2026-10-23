if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
        message(FATAL_ERROR "The Linux C ABI libraries require GCC or Clang with the GNU C++ runtime")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang)$")
        message(FATAL_ERROR "The macOS C ABI libraries require Clang with libc++")
    endif()
elseif(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "The C ABI libraries support Linux, macOS and Windows with an MSVC-compatible toolchain")
endif()

# Both implementation and bundled runtime definitions stay local to this shared object.
# Runtime archives must support PIC; no C++ exception or allocation ownership crosses the ABI.
function(cppjieba_configure_c_api_runtime target)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE -fno-gnu-unique)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(${target} PRIVATE
            -static-libstdc++ -static-libgcc
            "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map"
            "LINKER:--exclude-libs,ALL"
            "LINKER:--gc-sections"
            "LINKER:--undefined=__cxa_pure_virtual"
            "LINKER:-z,defs"
            "LINKER:-z,text"
        )
        # Discard unused transactional-runtime sections, and resolve weak pure-virtual references privately.
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
    elseif(APPLE)
        get_filename_component(c_api_compiler "${CMAKE_CXX_COMPILER}" REALPATH)
        get_filename_component(c_api_compiler_dir "${c_api_compiler}" DIRECTORY)
        get_filename_component(c_api_toolchain_dir "${c_api_compiler_dir}" DIRECTORY)
        # Override these cache paths when the matching PIC runtime archives live outside the compiler prefix.
        find_library(CPPJIEBA_C_API_LIBCXX NAMES libc++.a
            HINTS "${c_api_toolchain_dir}/lib/c++" "${c_api_toolchain_dir}/lib" NO_DEFAULT_PATH REQUIRED)
        find_library(CPPJIEBA_C_API_LIBCXXABI NAMES libc++abi.a
            HINTS "${c_api_toolchain_dir}/lib/c++" "${c_api_toolchain_dir}/lib" NO_DEFAULT_PATH REQUIRED)
        find_library(CPPJIEBA_C_API_LIBUNWIND NAMES libunwind.a
            HINTS "${c_api_toolchain_dir}/lib/unwind" "${c_api_toolchain_dir}/lib" NO_DEFAULT_PATH REQUIRED)
        target_link_options(${target} PRIVATE -nostdlib++
            "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/exports.darwin"
            "LINKER:-u,___cxa_pure_virtual"
            "LINKER:-dead_strip" "LINKER:-undefined,error")
        target_link_libraries(${target} PRIVATE
            "${CPPJIEBA_C_API_LIBCXX}" "${CPPJIEBA_C_API_LIBCXXABI}" "${CPPJIEBA_C_API_LIBUNWIND}")
        set_target_properties(${target} PROPERTIES MACOSX_RPATH YES INSTALL_NAME_DIR "@rpath")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.darwin")
    else()
        target_sources(${target} PRIVATE exports.def)
        set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        if(BUILD_TESTING)
            set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test")
        endif()
    endif()
endfunction()
