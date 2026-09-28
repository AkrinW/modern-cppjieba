if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../jieba-rs/jieba/Cargo.toml")
    message(FATAL_ERROR "Rust comparisons require jieba-rs. Run: git submodule update --init deps/jieba-rs")
endif()

find_program(CARGO_EXECUTABLE cargo HINTS "$ENV{HOME}/.cargo/bin" REQUIRED)

set(RUST_BENCH_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/cargo")
set(RUST_BENCH_LIB "${RUST_BENCH_TARGET_DIR}/release/${CMAKE_SHARED_LIBRARY_PREFIX}modern_cppjieba_rust_bench${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(RUST_BENCH_BYPRODUCTS "${RUST_BENCH_LIB}")
if(WIN32)
    set(RUST_BENCH_IMPLIB "${RUST_BENCH_LIB}${CMAKE_IMPORT_LIBRARY_SUFFIX}")
    list(APPEND RUST_BENCH_BYPRODUCTS "${RUST_BENCH_IMPLIB}")
endif()

# Cargo tracks Rust source dependencies, including changes in the upstream submodule.
add_custom_target(jieba_rs_build
    BYPRODUCTS ${RUST_BENCH_BYPRODUCTS}
    COMMAND "${CARGO_EXECUTABLE}" build --release --locked
            --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml" --target-dir "${RUST_BENCH_TARGET_DIR}"
    COMMENT "Building the pinned jieba-rs benchmark adapter (release)"
    VERBATIM
)

add_library(jieba_rs SHARED IMPORTED GLOBAL)
add_library(jieba_rs::jieba_rs ALIAS jieba_rs)
set_target_properties(jieba_rs PROPERTIES
    IMPORTED_LOCATION "${RUST_BENCH_LIB}"
    # Cargo's cdylib has no SONAME; link by library name so runtime lookup uses the build RPATH.
    IMPORTED_NO_SONAME TRUE
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
if(WIN32)
    set_target_properties(jieba_rs PROPERTIES IMPORTED_IMPLIB "${RUST_BENCH_IMPLIB}")
endif()
add_dependencies(jieba_rs jieba_rs_build)
