# Comparison dependencies

These dependencies are loaded only for legacy tests or benchmarks. The public
`neo_cppjieba::neo_cppjieba` target does not link to them or expose their headers.

| Dependency | Source | CMake target |
| --- | --- | --- |
| cppjieba | Official upstream submodule, pinned commit | `cppjieba::cppjieba` |
| limonp | Official upstream submodule, pinned commit | `limonp::limonp` |
| jieba-rs | Cargo registry, exactly 0.11.0 with `Cargo.lock` | `jieba_rs::jieba_rs` |
| Python jieba | Official upstream submodule, v0.42.1 | Standalone Python benchmark |

```sh
git submodule update --init deps/cppjieba deps/limonp
```

## Versions checked on 2026-09-22

- cppjieba: `8f171de5018e8478ff22ca58caacf579cba809c8`, upstream `master` HEAD.
- limonp: `4065c5f6d5a7a7248aacdd34d738fe0351b97c3f`, upstream `master` HEAD.
- jieba-rs: `0.11.0`, the latest non-yanked stable version in the crates.io index.

The submodule gitlinks and Cargo lockfile determine the versions used by a
checkout. An installed limonp package does not override the pinned source.

## C++ dependencies

`cppjieba.cmake` exposes the upstream headers without invoking its CMake project.
Upstream already includes the FullSegment null-match length fix previously kept
in our vendored snapshot, as well as the UTF-8 decoding interfaces. No local fork
or source patch is needed for these changes.

Current upstream cppjieba no longer depends on limonp. Legacy tests use cppjieba's
own formatting helpers; the FileIO comparison and legacy load benchmark link to
limonp explicitly. Neither upstream project's tests or installation rules are
pulled into our build.

The upstream facade's default paths resolve to its own `dict/` directory.
Segmentation comparisons explicitly pass our common dictionary, HMM model and
user-dictionary path, so updating the submodule's data does not change the common
benchmark inputs. The neo implementation and root `dict/` remain unchanged.
The former upstream guide is retained in [cppjieba-upstream-history.md](cppjieba-upstream-history.md).

## Rust dependency and adapter

`rust-jieba/` contains our C ABI adapter and C++ wrapper, not a copy of jieba-rs.
Cargo downloads the pinned library and locked dependencies when the Rust
benchmark is built. The adapter sources remain in this repository, and all build
outputs stay in the CMake build tree. See [the Rust benchmark guide](../benchmark/RUST.md).

## Python dependency

`python-jieba/` pins [fxsjy/jieba](https://github.com/fxsjy/jieba) at the
`v0.42.1` release, commit `1e20c89b66f56c9301b0feed211733ffaa1bd72a`.
Its upstream MIT license is retained in the submodule. Initialize it with:

```sh
git submodule update --init deps/python-jieba
```

The benchmark imports this checkout directly; no pip installation or Python
development headers are required. Python is not a dependency of the C++ library
or its ordinary build. The adapter reads the shared dictionary and HMM model
before timing without patching upstream code. See [the Python comparison guide](../benchmark/PYTHON.md).
