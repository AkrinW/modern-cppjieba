option(CPPJIEBA_C_API_BUILD_SHARED "Build the shared C ABI library with a private C++ runtime" ON)
option(CPPJIEBA_C_API_BUILD_STATIC "Build the static C ABI library using the consumer's C++ runtime" ON)
if(NOT CPPJIEBA_C_API_BUILD_SHARED AND NOT CPPJIEBA_C_API_BUILD_STATIC)
    message(FATAL_ERROR "CPPJIEBA_BUILD_C_API requires at least one C API library variant")
endif()

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

# Keep implementation headers in the build tree and export only the public C header directory.
function(configure_c_api_target target export_name)
    target_link_libraries(${target} PRIVATE "$<BUILD_INTERFACE:neo_cppjieba::neo_cppjieba>")
    target_include_directories(${target} PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    )
    set_target_properties(${target} PROPERTIES EXPORT_NAME ${export_name} POSITION_INDEPENDENT_CODE YES)
    install(TARGETS ${target} EXPORT neo_cppjieba_cTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
endfunction()

if(CPPJIEBA_C_API_BUILD_SHARED)
    add_library(neo_cppjieba_c SHARED src/neo_cppjieba.cpp)
    add_library(neo_cppjieba::c_shared ALIAS neo_cppjieba_c)
    add_library(neo_cppjieba::c ALIAS neo_cppjieba_c)
    configure_c_api_target(neo_cppjieba_c c_shared)
    target_compile_definitions(neo_cppjieba_c PRIVATE NEO_CPPJIEBA_C_BUILD)
    set_target_properties(neo_cppjieba_c PROPERTIES
        VERSION 1.0.0
        SOVERSION 1
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
    )
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(neo_cppjieba_c PRIVATE -fno-gnu-unique)
    endif()

    # Both implementation and bundled runtime definitions stay local to this shared object.
    # Runtime archives must support PIC; no C++ exception or allocation ownership crosses the ABI.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
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
        target_link_options(neo_cppjieba_c PRIVATE -nostdlib++
            "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/exports.darwin"
            "LINKER:-u,___cxa_pure_virtual"
            "LINKER:-dead_strip" "LINKER:-undefined,error")
        target_link_libraries(neo_cppjieba_c PRIVATE
            "${CPPJIEBA_C_API_LIBCXX}" "${CPPJIEBA_C_API_LIBCXXABI}" "${CPPJIEBA_C_API_LIBUNWIND}")
        set_target_properties(neo_cppjieba_c PROPERTIES MACOSX_RPATH YES INSTALL_NAME_DIR "@rpath")
        set_property(TARGET neo_cppjieba_c APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/exports.darwin")
    else()
        target_sources(neo_cppjieba_c PRIVATE exports.def)
        set_target_properties(neo_cppjieba_c PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        if(BUILD_TESTING)
            set_target_properties(neo_cppjieba_c PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/test")
        endif()
    endif()
endif()

if(CPPJIEBA_C_API_BUILD_STATIC)
    add_library(neo_cppjieba_c_static STATIC src/neo_cppjieba.cpp)
    add_library(neo_cppjieba::c_static ALIAS neo_cppjieba_c_static)
    configure_c_api_target(neo_cppjieba_c_static c_static)
    target_compile_definitions(neo_cppjieba_c_static PUBLIC NEO_CPPJIEBA_C_STATIC)
    # The Windows static archive must not overwrite the DLL's import library.
    if(NOT WIN32)
        set_target_properties(neo_cppjieba_c_static PROPERTIES OUTPUT_NAME neo_cppjieba_c)
    endif()
    if(NOT CPPJIEBA_C_API_BUILD_SHARED)
        add_library(neo_cppjieba::c ALIAS neo_cppjieba_c_static)
    endif()
endif()

install(FILES include/neo_cppjieba.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
include(CMakePackageConfigHelpers)
set(c_api_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/neo_cppjieba_c")
configure_package_config_file(neo_cppjieba_cConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/neo_cppjieba_cConfig.cmake" INSTALL_DESTINATION "${c_api_package_dir}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/neo_cppjieba_cConfigVersion.cmake"
    VERSION 1.0.0 COMPATIBILITY SameMajorVersion)
install(EXPORT neo_cppjieba_cTargets NAMESPACE neo_cppjieba:: DESTINATION "${c_api_package_dir}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/neo_cppjieba_cConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/neo_cppjieba_cConfigVersion.cmake" DESTINATION "${c_api_package_dir}")
