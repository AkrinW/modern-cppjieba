# Comparison dependencies

These dependencies are loaded only for legacy tests or benchmarks. The public
`neo_cppjieba::neo_cppjieba` target does not link to them or expose their headers.

| Dependency | Source | CMake target |
| --- | --- | --- |
| old cppjieba | Vendored snapshot of this repository's legacy implementation | `cppjieba::cppjieba` |
| limonp | Installed CMake package, or the pinned `deps/limonp` submodule | `limonp::limonp` |
| jieba-rs | Cargo registry, exactly 0.11.0 with `Cargo.lock` | `jieba_rs::jieba_rs` |

## old cppjieba

`cppjieba/include/cppjieba/` preserves the headers from commit
`9df2451` byte for byte, including the local FullSegment and Unicode fixes.
It is a separate header-only comparison dependency, not part of the installed
neo headers. Updating this snapshot is a separate benchmark baseline change.

The `cppjieba/dict` symlink points to the repository's existing dictionaries.
This retains the legacy facade's default dictionary lookup relative to
`Jieba.hpp`, including its implicit IDF and stop-word paths. Checkouts must
preserve symlinks when using the legacy dependency.

## limonp

Initialize the pinned submodule when an installed `limonp::limonp` target is not
available:

```sh
git submodule update --init deps/limonp
```

The adapter exposes only the headers and does not run limonp's own CMake project,
which would also enable its tests. Legacy cppjieba links to this target transitively.

## jieba-rs

`rust-jieba/` contains the C ABI adapter and C++ wrapper used by the comparison
benchmark. Cargo downloads the pinned Rust library and its locked dependencies
only when the Rust benchmark is built. Outputs stay in the CMake build tree.
See [the Rust benchmark guide](../benchmark/RUST.md) for usage and interpretation.
