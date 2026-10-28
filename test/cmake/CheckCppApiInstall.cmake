set(work_dir "${BUILD_DIR}/cpp-api-install/${CONFIG}")
set(prefix "${work_dir}/staging")
set(relocated_prefix "${work_dir}/relocated")
file(REMOVE_RECURSE "${work_dir}")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --config "${CONFIG}"
    --prefix "${prefix}" --component neo_cppjieba COMMAND_ERROR_IS_FATAL ANY)
file(RENAME "${prefix}" "${relocated_prefix}")

include("${CMAKE_CURRENT_LIST_DIR}/ConsumerConfig.cmake")
list(APPEND configure_args
    "-Dneo_cppjieba_DIR=${relocated_prefix}/${INSTALL_LIBDIR}/cmake/neo_cppjieba"
    "-DCMAKE_EXE_LINKER_FLAGS=${EXE_LINKER_FLAGS}" "-DICU_ROOT=${ICU_ROOT}")

# Configure independent consumers to exercise package discovery, compilation, and runtime dependencies.
function(cppjieba_check_consumer name expectation)
    set(consumer_build "${work_dir}/${name}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}/test/cpp_api_consumer"
        -B "${consumer_build}" ${configure_args} ${ARGN}
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(expectation STREQUAL "reject")
        if(result EQUAL 0)
            message(FATAL_ERROR "Consumer ${name} unexpectedly accepted an unavailable component")
        endif()
        return()
    endif()
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Consumer ${name} failed to configure:\n${output}\n${error}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}" --config "${CONFIG}"
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${CTEST}" --test-dir "${consumer_build}" -C "${CONFIG}" --output-on-failure
        COMMAND_ERROR_IS_FATAL ANY)
endfunction()

cppjieba_check_consumer(core accept "-DCMAKE_DISABLE_FIND_PACKAGE_ICU=TRUE")
cppjieba_check_consumer(optional_icu accept
    "-DCPPJIEBA_TEST_COMPONENT=icu" "-DCPPJIEBA_TEST_OPTIONAL=ON" "-DCMAKE_DISABLE_FIND_PACKAGE_ICU=TRUE")
cppjieba_check_consumer(unknown_component reject "-DCPPJIEBA_TEST_COMPONENT=unknown")
cppjieba_check_consumer(missing_icu reject
    "-DCPPJIEBA_TEST_COMPONENT=icu" "-DCMAKE_DISABLE_FIND_PACKAGE_ICU=TRUE")
if(ENABLE_ICU)
    cppjieba_check_consumer(icu accept "-DCPPJIEBA_TEST_COMPONENT=icu")
endif()
