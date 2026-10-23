set(prefix "${BUILD_DIR}/c-api-install/${CONFIG}")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}/api" --config "${CONFIG}" --prefix "${prefix}"
    COMMAND_ERROR_IS_FATAL ANY)

set(configure_args -G "${GENERATOR}"
    "-DCMAKE_BUILD_TYPE=${CONFIG}"
    "-DCMAKE_C_COMPILER=${C_COMPILER}"
    "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
    "-DCMAKE_CXX_FLAGS=${CXX_FLAGS}"
    "-Dneo_cppjieba_c_DIR=${prefix}/${INSTALL_LIBDIR}/cmake/neo_cppjieba_c"
    "-DTEST_PATHS_DIR=${BUILD_DIR}/generated")
if(NOT GENERATOR_PLATFORM STREQUAL "")
    list(APPEND configure_args -A "${GENERATOR_PLATFORM}")
endif()
if(NOT GENERATOR_TOOLSET STREQUAL "")
    list(APPEND configure_args -T "${GENERATOR_TOOLSET}")
endif()
foreach(setting IN ITEMS MSVC_RUNTIME_LIBRARY OSX_ARCHITECTURES OSX_SYSROOT OSX_DEPLOYMENT_TARGET)
    if(NOT "${${setting}}" STREQUAL "")
        list(APPEND configure_args "-DCMAKE_${setting}=${${setting}}")
    endif()
endforeach()

foreach(variant IN ITEMS shared static)
    string(TOUPPER "${variant}" variant_upper)
    if(NOT BUILD_${variant_upper})
        continue()
    endif()
    set(consumer_build "${BUILD_DIR}/c-api-consumer-${variant}/${CONFIG}")
    set(linker_flags "")
    if(variant STREQUAL "static")
        set(linker_flags "${EXE_LINKER_FLAGS}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${CONSUMER_DIR}" -B "${consumer_build}" ${configure_args}
        "-DC_API_VARIANT=${variant}" "-DCMAKE_EXE_LINKER_FLAGS=${linker_flags}" COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}" --config "${CONFIG}"
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${CTEST}" --test-dir "${consumer_build}" -C "${CONFIG}" --output-on-failure
        COMMAND_ERROR_IS_FATAL ANY)
endforeach()
