set(prefix "${BUILD_DIR}/c-api-install/${CONFIG}")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}/api" --config "${CONFIG}" --prefix "${prefix}"
    COMMAND_ERROR_IS_FATAL ANY)

include("${CMAKE_CURRENT_LIST_DIR}/ConsumerConfig.cmake")
list(APPEND configure_args
    "-Dneo_cppjieba_c_DIR=${prefix}/${INSTALL_LIBDIR}/cmake/neo_cppjieba_c"
    "-DTEST_PATHS_DIR=${BUILD_DIR}/generated")

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
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}/test/c_api_consumer"
        -B "${consumer_build}" ${configure_args}
        "-DC_API_VARIANT=${variant}" "-DCMAKE_EXE_LINKER_FLAGS=${linker_flags}" COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}" --config "${CONFIG}"
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${CTEST}" --test-dir "${consumer_build}" -C "${CONFIG}" --output-on-failure
        COMMAND_ERROR_IS_FATAL ANY)
endforeach()
