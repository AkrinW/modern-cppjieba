execute_process(COMMAND "${NM}" -D --defined-only --format=posix "${LIBRARY}"
    OUTPUT_VARIABLE definitions COMMAND_ERROR_IS_FATAL ANY)
set(expected
    neo_jieba_v1_max_input_bytes
    neo_jieba_v1_engine_create
    neo_jieba_v1_engine_destroy
    neo_jieba_v1_cut_utf8_owned
    neo_jieba_v1_tokens_borrow_data
    neo_jieba_v1_tokens_destroy)
set(actual)
string(REPLACE "\n" ";" lines "${definitions}")
foreach(line IN LISTS lines)
    if(line STREQUAL "" OR line MATCHES "^NEO_CPPJIEBA_C_1\\.0(@@NEO_CPPJIEBA_C_1\\.0)? A ")
        continue()
    endif()
    if(NOT line MATCHES "^(neo_jieba_v1_[a-z0-9_]+)@@NEO_CPPJIEBA_C_1\\.0 T ")
        message(FATAL_ERROR "Unexpected dynamic definition: ${line}")
    endif()
    list(APPEND actual "${CMAKE_MATCH_1}")
endforeach()
list(SORT actual)
list(SORT expected)
if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "C ABI export set changed: ${actual}")
endif()

execute_process(COMMAND "${NM}" -D --undefined-only --format=posix "${LIBRARY}"
    OUTPUT_VARIABLE imports COMMAND_ERROR_IS_FATAL ANY)
if(imports MATCHES "GLIBCXX_|CXXABI_|(^|\n)(_Z|_Unwind_|__gxx_)")
    message(FATAL_ERROR "C++ runtime imports escaped the private runtime: ${imports}")
endif()
string(REPLACE "\n" ";" lines "${imports}")
foreach(line IN LISTS lines)
    if(line MATCHES "^__cxa_" AND NOT line MATCHES "^__cxa_(atexit|finalize|thread_atexit_impl)@GLIBC_")
        message(FATAL_ERROR "Unexpected C++ ABI import: ${line}")
    endif()
endforeach()

execute_process(COMMAND "${READELF}" --dynamic --wide "${LIBRARY}"
    OUTPUT_VARIABLE dynamic COMMAND_ERROR_IS_FATAL ANY)
if(NOT dynamic MATCHES "SONAME[^\n]*\\[libneo_cppjieba_c\\.so\\.1\\]")
    message(FATAL_ERROR "Missing or changed ABI v1 SONAME")
endif()
if(dynamic MATCHES "NEEDED[^\n]*(libstdc\\+\\+|libc\\+\\+|libgcc_s|libunwind)")
    message(FATAL_ERROR "A shared C++ runtime is still required: ${dynamic}")
endif()
message(STATUS "C ABI exports, SONAME and private runtime verified")
