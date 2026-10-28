include(CMakePackageConfigHelpers)

set(cppjieba_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/neo_cppjieba")
set(cppjieba_package_build_dir "${PROJECT_BINARY_DIR}/cmake")
set(cppjieba_dict_dir "${CMAKE_INSTALL_DATADIR}/neo_cppjieba/dict")

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/neo"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" COMPONENT neo_cppjieba)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/dict/"
    DESTINATION "${cppjieba_dict_dir}" COMPONENT neo_cppjieba)
# Transitional copy for consumers of the old location, which collides with upstream CppJieba installs.
if(CPPJIEBA_INSTALL_LEGACY_DICT_DIR)
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/dict/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/cppjieba/dict" COMPONENT neo_cppjieba)
endif()
install(TARGETS neo_cppjieba EXPORT neo_cppjiebaTargets COMPONENT neo_cppjieba)
install(EXPORT neo_cppjiebaTargets NAMESPACE neo_cppjieba::
    DESTINATION "${cppjieba_package_dir}" COMPONENT neo_cppjieba)

# Keep ICU out of the core export so core-only consumers need no optional dependencies.
if(CPPJIEBA_ENABLE_ICU)
    install(TARGETS neo_cppjieba_icu EXPORT neo_cppjiebaIcuTargets COMPONENT neo_cppjieba)
    install(EXPORT neo_cppjiebaIcuTargets NAMESPACE neo_cppjieba::
        DESTINATION "${cppjieba_package_dir}" COMPONENT neo_cppjieba)
endif()

configure_package_config_file("${CMAKE_CURRENT_LIST_DIR}/neo_cppjiebaConfig.cmake.in"
    "${cppjieba_package_build_dir}/neo_cppjiebaConfig.cmake"
    INSTALL_DESTINATION "${cppjieba_package_dir}"
    PATH_VARS cppjieba_dict_dir)
write_basic_package_version_file("${cppjieba_package_build_dir}/neo_cppjiebaConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion ARCH_INDEPENDENT)
install(FILES
    "${cppjieba_package_build_dir}/neo_cppjiebaConfig.cmake"
    "${cppjieba_package_build_dir}/neo_cppjiebaConfigVersion.cmake"
    DESTINATION "${cppjieba_package_dir}" COMPONENT neo_cppjieba)
