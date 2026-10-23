option(CPPJIEBA_C_API_BUILD_SHARED "Build the shared C ABI library with a private C++ runtime" ON)
option(CPPJIEBA_C_API_BUILD_STATIC "Build the static C ABI library using the consumer's C++ runtime" ON)
if(NOT CPPJIEBA_C_API_BUILD_SHARED AND NOT CPPJIEBA_C_API_BUILD_STATIC)
    message(FATAL_ERROR "CPPJIEBA_BUILD_C_API requires at least one C API library variant")
endif()

include(cmake/CAbiPlatform.cmake)

# Keep implementation headers in the build tree and export only the public C header directory.
function(cppjieba_configure_c_api_target target export_name)
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
    cppjieba_configure_c_api_target(neo_cppjieba_c c_shared)
    target_compile_definitions(neo_cppjieba_c PRIVATE NEO_CPPJIEBA_C_BUILD)
    set_target_properties(neo_cppjieba_c PROPERTIES
        VERSION "${PROJECT_VERSION}"
        SOVERSION 1
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        WINDOWS_EXPORT_ALL_SYMBOLS OFF
    )
    cppjieba_configure_c_api_runtime(neo_cppjieba_c)
endif()

if(CPPJIEBA_C_API_BUILD_STATIC)
    add_library(neo_cppjieba_c_static STATIC src/neo_cppjieba.cpp)
    add_library(neo_cppjieba::c_static ALIAS neo_cppjieba_c_static)
    cppjieba_configure_c_api_target(neo_cppjieba_c_static c_static)
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
configure_package_config_file(cmake/neo_cppjieba_cConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/neo_cppjieba_cConfig.cmake" INSTALL_DESTINATION "${c_api_package_dir}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/neo_cppjieba_cConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
install(EXPORT neo_cppjieba_cTargets NAMESPACE neo_cppjieba:: DESTINATION "${c_api_package_dir}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/neo_cppjieba_cConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/neo_cppjieba_cConfigVersion.cmake" DESTINATION "${c_api_package_dir}")
