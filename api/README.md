# C ABI libraries

The v1 C API supports UTF-8 segmentation with independently owned results. Its six
functions, fixed-width fields, status values and ownership contract are declared in
`include/neo_cppjieba.h`. Shared and static builds expose the same API.

```sh
cmake -S . -B build-api -DCPPJIEBA_BUILD_C_API=ON -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-api --config Release
cmake --install build-api --config Release --prefix install
```

`CPPJIEBA_C_API_BUILD_SHARED` and `CPPJIEBA_C_API_BUILD_STATIC` both default to `ON`.
Disable either to build only the other variant; at least one must remain enabled.
`CPPJIEBA_BUILD_C_API` defaults to `OFF`, preserving the header-only build.

| Platform in CI | Shared library | Static library |
| --- | --- | --- |
| Linux x64/ARM64, GCC 15 | `libneo_cppjieba_c.so.1` | `libneo_cppjieba_c.a` |
| macOS, LLVM 23/libc++ | `libneo_cppjieba_c.1.dylib` | `libneo_cppjieba_c.a` |
| Windows x64, MSVC 2026 | `neo_cppjieba_c.dll` and import library `neo_cppjieba_c.lib` | `neo_cppjieba_c_static.lib` |

All platforms build and test Debug and Release variants. ABI compatibility applies
within the same platform ABI; architecture, deployment target and system-library
requirements still apply. Applications supply dictionary/model paths explicitly.

## Linking an installed package

For a shared-library consumer, C is sufficient:

```cmake
project(example LANGUAGES C)
find_package(neo_cppjieba_c 1 REQUIRED COMPONENTS shared)
add_executable(example main.c)
target_link_libraries(example PRIVATE neo_cppjieba::c_shared)
```

Static consumers enable the C++ linker/runtime even if their sources are all C:

```cmake
project(example LANGUAGES C CXX)
find_package(neo_cppjieba_c 1 REQUIRED COMPONENTS static)
add_executable(example main.c)
target_link_libraries(example PRIVATE neo_cppjieba::c_static)
```

Set `CMAKE_PREFIX_PATH` to the installation prefix. Both targets carry the public
include directory; `c_static` also defines `NEO_CPPJIEBA_C_STATIC`. When linking
manually, define that macro for static consumers, especially on Windows, and link
with a compatible C++ runtime. `neo_cppjieba::c` selects shared when present,
otherwise static. Prefer an explicit target when linkage matters.

## Runtime and symbol boundaries

Shared libraries export only the six C functions. Linux uses an ELF version
script and private static GNU C++/unwind runtimes. macOS uses an exported-symbol
list and private libc++/libc++abi/libunwind archives. Windows uses explicit exports
and a private `/MT` or `/MTd` runtime. C++ exceptions and allocation ownership remain
inside the shared library; token results must be destroyed through its C API.

The macOS build locates the runtime archives beside the selected compiler, matching
the Homebrew LLVM layout used by CI. Override `CPPJIEBA_C_API_LIBCXX`,
`CPPJIEBA_C_API_LIBCXXABI`, and `CPPJIEBA_C_API_LIBUNWIND` with matching PIC archive
paths when using another layout. A missing archive is a configuration error.

Static archives contain the adapter and instantiated implementation, without a
private C++ runtime or an independent symbol boundary. The final application owns
runtime selection and symbol visibility. On MSVC, the static target honors
`CMAKE_MSVC_RUNTIME_LIBRARY`; consumers must use the same runtime setting and a
compatible build configuration. The static `.lib` is distinct from the DLL import
library and must not be substituted for it without the static compile definition.

With `BUILD_TESTING=ON`, the suite tests both variants, shared-library exports and
runtime dependencies, and separate installed C consumers. Linux additionally tests
the shared library in a C host with conflicting C++ runtime symbols.
