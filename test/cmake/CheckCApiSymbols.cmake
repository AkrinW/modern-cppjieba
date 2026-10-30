# Symbols added after the 1.2.0 release carry the NEO_CPPJIEBA_C_1.1 version node on Linux.
set(expected_1_1
    neo_jieba_v1_workspace_create
    neo_jieba_v1_workspace_destroy
    neo_jieba_v1_ranges_create
    neo_jieba_v1_ranges_destroy
    neo_jieba_v1_cut_utf8_into
    neo_jieba_v1_ranges_borrow_data
    neo_jieba_v1_engine_create_from_sources
    neo_jieba_v1_engine_create_from_snapshot
    neo_jieba_v1_write_dict_snapshot
    neo_jieba_v1_cut_utf8_tagged_into
    neo_jieba_v1_lookup_tag
    neo_jieba_v1_engine_clone
    neo_jieba_v1_engine_add_word
    neo_jieba_v1_engine_add_word_with_freq
    neo_jieba_v1_engine_remove_word
    neo_jieba_v1_engine_compact
    neo_jieba_v1_tfidf_create
    neo_jieba_v1_tfidf_destroy
    neo_jieba_v1_textrank_create
    neo_jieba_v1_textrank_destroy
    neo_jieba_v1_keywords_create
    neo_jieba_v1_keywords_destroy
    neo_jieba_v1_tfidf_extract_into
    neo_jieba_v1_textrank_extract_into
    neo_jieba_v1_keywords_borrow_data)
set(expected
    neo_jieba_v1_max_input_bytes
    neo_jieba_v1_engine_create
    neo_jieba_v1_engine_destroy
    neo_jieba_v1_cut_utf8_owned
    neo_jieba_v1_tokens_borrow_data
    neo_jieba_v1_tokens_destroy
    ${expected_1_1})
set(actual)
if(PLATFORM STREQUAL "Linux")
    execute_process(COMMAND "${NM}" -D --defined-only --format=posix "${LIBRARY}"
        OUTPUT_VARIABLE definitions COMMAND_ERROR_IS_FATAL ANY)
    string(REPLACE "\n" ";" lines "${definitions}")
    foreach(line IN LISTS lines)
        if(line STREQUAL "" OR line MATCHES "^NEO_CPPJIEBA_C_1\\.[01](@@NEO_CPPJIEBA_C_1\\.[01])? A ")
            continue()
        endif()
        if(NOT line MATCHES "^(neo_jieba_v1_[a-z0-9_]+)@@NEO_CPPJIEBA_C_(1\\.[01]) T ")
            message(FATAL_ERROR "Unexpected dynamic definition: ${line}")
        endif()
        set(symbol "${CMAKE_MATCH_1}")
        set(node "${CMAKE_MATCH_2}")
        # Released symbols keep their original node so existing binaries still resolve them.
        set(expected_node "1.0")
        list(FIND expected_1_1 "${symbol}" added_index)
        if(NOT added_index EQUAL -1)
            set(expected_node "1.1")
        endif()
        if(NOT node STREQUAL expected_node)
            message(FATAL_ERROR "${symbol} has version node ${node}, expected ${expected_node}")
        endif()
        list(APPEND actual "${symbol}")
    endforeach()

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
elseif(PLATFORM STREQUAL "Darwin")
    execute_process(COMMAND "${NM}" -g -U -j "${LIBRARY}"
        OUTPUT_VARIABLE definitions COMMAND_ERROR_IS_FATAL ANY)
    string(REPLACE "\n" ";" lines "${definitions}")
    foreach(line IN LISTS lines)
        if(line STREQUAL "")
            continue()
        endif()
        if(NOT line MATCHES "^_(neo_jieba_v1_[a-z0-9_]+)$")
            message(FATAL_ERROR "Unexpected dynamic definition: ${line}")
        endif()
        list(APPEND actual "${CMAKE_MATCH_1}")
    endforeach()
    execute_process(COMMAND "${OTOOL}" -L "${LIBRARY}"
        OUTPUT_VARIABLE dynamic COMMAND_ERROR_IS_FATAL ANY)
    if(NOT dynamic MATCHES "@rpath/libneo_cppjieba_c\\.1\\.dylib")
        message(FATAL_ERROR "Missing or changed ABI v1 install name: ${dynamic}")
    endif()
    if(dynamic MATCHES "/lib(c\\+\\+|stdc\\+\\+|unwind|gcc_s)[^/\n]*\\.dylib")
        message(FATAL_ERROR "A shared C++ runtime is still required: ${dynamic}")
    endif()
    execute_process(COMMAND "${NM}" -u -j "${LIBRARY}"
        OUTPUT_VARIABLE imports COMMAND_ERROR_IS_FATAL ANY)
    if(imports MATCHES "(^|\n)(__Z|__Unwind_|___gxx_)")
        message(FATAL_ERROR "C++ runtime imports escaped the private runtime: ${imports}")
    endif()
elseif(PLATFORM STREQUAL "Windows")
    execute_process(COMMAND "${DUMPBIN}" /NOLOGO /EXPORTS "${LIBRARY}"
        OUTPUT_VARIABLE definitions COMMAND_ERROR_IS_FATAL ANY)
    string(REPLACE "\r" "" definitions "${definitions}")
    string(REPLACE "\n" ";" lines "${definitions}")
    foreach(line IN LISTS lines)
        if(line MATCHES "^[ \t]*[0-9]+[ \t]+[0-9A-Fa-f]+[ \t]+[0-9A-Fa-f]+[ \t]+([^ \t]+)")
            list(APPEND actual "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    execute_process(COMMAND "${DUMPBIN}" /NOLOGO /DEPENDENTS "${LIBRARY}"
        OUTPUT_VARIABLE dynamic COMMAND_ERROR_IS_FATAL ANY)
    string(TOUPPER "${dynamic}" dynamic)
    if(dynamic MATCHES "(MSVCP|VCRUNTIME|UCRTBASED)[^ \t\r\n]*\\.DLL")
        message(FATAL_ERROR "A shared C++ runtime is still required: ${dynamic}")
    endif()
else()
    message(FATAL_ERROR "Unsupported C ABI audit platform: ${PLATFORM}")
endif()

list(SORT actual)
list(SORT expected)
if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "C ABI export set changed: ${actual}")
endif()
message(STATUS "C ABI exports and private runtime verified on ${PLATFORM}")
