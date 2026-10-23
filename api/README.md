# C ABI libraries

[Build guide](../docs/building.md) · [Public C header](include/neo_cppjieba.h) · [Release notes](../CHANGELOG.md)

Release 1.2.0 adds the v1 C API for UTF-8 segmentation and language bindings.
Shared and static libraries expose the same six functions, fixed-width fields and
independently owned results. Package versions follow the project release; the ABI
major version and shared-library `SOVERSION` remain `1`.

## Build and install

```sh
cmake -S . -B build-api -DCPPJIEBA_BUILD_C_API=ON -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-api --config Release
cmake --install build-api --config Release --prefix install
```

`CPPJIEBA_BUILD_C_API` defaults to `OFF`. When enabled, both
`CPPJIEBA_C_API_BUILD_SHARED` and `CPPJIEBA_C_API_BUILD_STATIC` default to `ON`;
select at least one library form.

| Platform in CI | Shared library | Static library |
| --- | --- | --- |
| Linux x64/ARM64, GCC 15 | `libneo_cppjieba_c.so.1` | `libneo_cppjieba_c.a` |
| macOS ARM64, LLVM 23/libc++ | `libneo_cppjieba_c.1.dylib` | `libneo_cppjieba_c.a` |
| Windows x64, MSVC 2026 | `neo_cppjieba_c.dll` and import library `neo_cppjieba_c.lib` | `neo_cppjieba_c_static.lib` |

CI covers Debug and Release builds. Binary compatibility applies within the same
platform ABI, with matching architecture, deployment target and system requirements.
Deploy the dictionary and model files with the application and pass their paths explicitly.

## Link an installed package

Set `CMAKE_PREFIX_PATH` to the installation prefix. A shared-library consumer uses C:

```cmake
project(example LANGUAGES C)
find_package(neo_cppjieba_c 1.2 CONFIG REQUIRED COMPONENTS shared)
add_executable(example main.c)
target_link_libraries(example PRIVATE neo_cppjieba::c_shared)
```

Static consumers enable the C++ linker and runtime for their C sources:

```cmake
project(example LANGUAGES C CXX)
find_package(neo_cppjieba_c 1.2 CONFIG REQUIRED COMPONENTS static)
add_executable(example main.c)
target_link_libraries(example PRIVATE neo_cppjieba::c_static)
```

Both targets carry the public include directory; `c_static` also defines
`NEO_CPPJIEBA_C_STATIC`. Manual static linking requires that definition and a compatible
C++ runtime. `neo_cppjieba::c` selects shared when available, then static; use explicit
targets to choose the linkage. A complete C caller is available in
[c_api_smoke.c](../test/unittest_neo/c_api_smoke.c).

## Calls and ownership

| Function | Purpose |
| --- | --- |
| `neo_jieba_v1_max_input_bytes` | Query this build's conservative input byte limit |
| `neo_jieba_v1_engine_create` | Load dictionary/model paths and return an engine |
| `neo_jieba_v1_cut_utf8_owned` | Segment a byte buffer with an explicit length and return an owned result |
| `neo_jieba_v1_tokens_borrow_data` | Read the result's UTF-8 text and token byte ranges |
| `neo_jieba_v1_tokens_destroy` | Release a result and its borrowed storage |
| `neo_jieba_v1_engine_destroy` | Release an engine |

Create an engine with nonempty main dictionary and model paths; an empty user
dictionary path selects the main dictionary alone. Input pointers are borrowed during
the call. Results own a copy of the input and all token ranges, independently of the
engine and earlier results. Engines may be destroyed after producing results.

Read each token with its `byte_offset` and `byte_length`. Lengths are authoritative:
text may contain NUL bytes and individual tokens are not NUL-terminated.
Borrowed text/range pointers remain valid until their result is destroyed; release
the result through `tokens_destroy` in the library that created it. NULL is accepted
by both destroy functions and for zero-length input.

Serialize calls that share an engine. Input, output and diagnostic buffers use
separate live storage. Release a previous handle before reusing its output slot,
finish reads before destroying a result, and release all handles before unloading
the library. FULL and SEARCH can return overlapping byte ranges.

## Status and diagnostics

The public header defines `OK`, `INVALID_ARGUMENT`, `OPERATION_FAILED`,
`OUT_OF_MEMORY` and `LIMIT_EXCEEDED`. Invalid UTF-8 and dictionary/model failures
return `OPERATION_FAILED`. The byte-limit query describes representable sizes;
allocations can still fail within that limit.

Pass a caller-owned `neo_jieba_error_buffer_v1`, NULL to omit diagnostics, or
`{NULL, 0, 0}` to request the required message length. `message_size` reports the
full byte length excluding NUL. Positive buffer capacity receives a NUL-terminated
prefix; truncation preserves the operation's status. Successful calls clear the message.

Failed create/cut calls clear their output handles. A failed borrow call clears a
valid output descriptor. C++ exceptions are translated into status values inside
the adapter; internal invariant failures remain fatal.

## Runtime and validation

Shared libraries export the six C functions and bundle private C++ runtimes:
GNU C++/unwind archives on Linux, libc++/libc++abi/libunwind on macOS, and `/MT`
or `/MTd` on Windows. Allocation ownership stays inside the library.

The macOS build searches beside the selected compiler, following the Homebrew LLVM
layout in CI. Set `CPPJIEBA_C_API_LIBCXX`, `CPPJIEBA_C_API_LIBCXXABI` and
`CPPJIEBA_C_API_LIBUNWIND` to matching PIC archives for other layouts.

Static libraries use the final application's runtime and symbol visibility.
On MSVC, match `CMAKE_MSVC_RUNTIME_LIBRARY` and the build configuration between
the library and its consumers. The static `.lib` and DLL import `.lib` serve their
respective targets and compile definitions.

With `BUILD_TESTING=ON`, tests cover both variants, exported symbols, runtime
dependencies and installed C consumers. Linux also checks a C host with conflicting
C++ runtime symbols. Configuration commands are listed in the [build guide](../docs/building.md#测试).
