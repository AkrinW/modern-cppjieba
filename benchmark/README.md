# Benchmarks

All benchmark executables live here and are enabled with
`CPPJIEBA_BUILD_BENCHMARKS=ON`. They are separate from GoogleTest and CTest.

```sh
git submodule update --init deps/limonp
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DCPPJIEBA_BUILD_BENCHMARKS=ON
cmake --build build-bench --parallel
```

Executables are written to `build-bench/benchmark/` (with a configuration
subdirectory for multi-configuration generators).

| Target | Measures | Comparison dependency |
| --- | --- | --- |
| `jieba_benchmark` | End-to-end segmentation | old cppjieba |
| `dict_trie_benchmark` | Dictionary loading and lookup | old cppjieba |
| `dag_benchmark` | DAG construction | old cppjieba |
| `trie_benchmark` | Trie construction and queries | old cppjieba |
| `fileio_benchmark` | Dictionary parsing with streams and mapped views | limonp |
| `load_test` | Legacy segmentation and keyword extraction throughput | old cppjieba |
| `unicode_benchmark` | Unicode decoding and encoding | none |
| `prefilter_benchmark` | Input prefiltering | none |
| `query_segment_benchmark` | Query segmentation strategies | none |
| `cut_compare_benchmark` | Old, neo and Rust segmentation | old cppjieba, jieba-rs |

The first nine targets are built by the benchmark option. The Rust target also
requires `CPPJIEBA_BUILD_RUST_BENCHMARKS=ON` and Cargo. Its adapter links through
`jieba_rs::jieba_rs`; see [RUST.md](RUST.md) for the output contracts and commands.

Shared input paths still refer to the repository's `dict/` and `test/testdata/`.
`QuerySegmentCompare.hpp` in `test/` is shared by query tests and the corresponding
benchmark; it does not depend on GoogleTest.

The FileIO benchmark was extracted from `FileIOPerfTest.MmapVsIfstream`.
It retains input validation and timing output, while correctness tests remain
in `test/unittest_neo/fileio_test.cpp`. Timing ratios are observations and do not
decide whether the unit tests pass.
