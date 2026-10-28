add_executable(test.run
    ../gtest_main.cpp
    keyword_extractor_test.cpp
    trie_test.cpp
    segments_test.cpp
    pos_tagger_test.cpp
    jieba_test.cpp
    pre_filter_test.cpp
    unicode_test.cpp
    textrank_test.cpp
    keyword_compare_test.cpp
)

target_compile_definitions(test.run PRIVATE LOGGING_LEVEL=LL_WARNING)
# Comparison tests expect bit-identical scores from both libraries; FMA contraction on FMA-capable targets
# (for example x86-64-v3 toolchain defaults) would round the two implementations differently.
target_compile_options(test.run PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-ffp-contract=off>)
# Add include directories
target_link_libraries(test.run PRIVATE cppjieba::cppjieba neo_cppjieba::neo_cppjieba cppjieba_test_support)
add_test(NAME legacy_unit_tests COMMAND test.run)
