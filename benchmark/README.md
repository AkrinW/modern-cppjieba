# Benchmarks

All benchmark executables live here and are enabled with
`CPPJIEBA_BUILD_BENCHMARKS=ON`. They are separate from GoogleTest and CTest.

Keep benchmark measurements, environment records and raw logs locally under
`benchmark/results/`. This directory is ignored by Git.

```sh
git submodule update --init deps/cppjieba deps/limonp
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
| `python_compare_benchmark.py` | The same native paths plus Python `list[str]` | `cut_compare_benchmark`, Python jieba |

The first nine targets are built by the benchmark option. The Rust target also
requires `CPPJIEBA_BUILD_RUST_BENCHMARKS=ON` and Cargo. Its adapter links through
`jieba_rs::jieba_rs`; see [RUST.md](RUST.md) for the output contracts and commands.

The Python script runs the native benchmark and Python serially using the same
corpus, dictionary, HMM model, rounds and samples. It requires Python 3.11 or
newer and the `deps/python-jieba` submodule; see [PYTHON.md](PYTHON.md).

Shared input paths still refer to the repository's `dict/` and `test/testdata/`.
`QuerySegmentCompare.hpp` in `test/` is shared by query tests and the corresponding
benchmark; it does not depend on GoogleTest.

The FileIO benchmark was extracted from `FileIOPerfTest.MmapVsIfstream`.
It retains input validation and timing output, while correctness tests remain
in `test/unittest_neo/fileio_test.cpp`. Timing ratios are observations and do not
decide whether the unit tests pass.

## GitHub Actions

CI runs Debug and Release unit tests on Linux x64, Linux ARM64, macOS and Windows.
Linux x64 also exercises the optional legacy C++ tests in both configurations.
JUnit results and CTest logs are uploaded for each matrix job, including failed tests.

The Linux `Release benchmarks / Rust=ON` job builds the C++ benchmarks and the
pinned Rust adapter, runs the Rust and Python adapter tests, then compares old C++,
neo C++, Rust and Python through `python_compare_benchmark.py`. The `Rust=OFF` job
continues to check that the standalone C++ benchmarks build without Cargo.

All four implementations run serially on one allowed CPU of the same runner,
using the common dictionary and HMM model. CI uses GCC 14, Rust 1.94.0 and Python
3.12; comparison dependencies are pinned by submodule revisions and `Cargo.lock`.

| Corpus | Rounds per sample | Samples |
| --- | ---: | ---: |
| `testlines.utf8` | 10,000 | 5 |
| `weicheng.utf8` | 3 | 5 |

The job summary shows timings for MIX, MP, FULL and SEARCH and the output
differences, plus the standalone old/neo C++ HMM comparison. Columns are grouped
by implementation: old C++, neo owned/borrowed/reused, Rust owned/borrowed, Python.
The native JSON exports HMM medians, minima, maxima and output differences in `hmm`.
The Python script writes a clean Markdown table beside each combined JSON report;
CI uses these tables for summaries and PR comments. Warnings and progress output
remain in the raw logs. The `benchmarks-linux-x64-release` artifact retains Markdown,
combined JSON, native JSON, raw logs and compiler/CPU information for 14 days. Reports stay
under the ignored `benchmark/results/ci/` directory.

Build, test and benchmark execution errors fail CI. Timing ratios have no pass/fail
threshold on shared runners; the existing benchmark suppresses rankings when
segmentation results differ. macOS and Windows compile the C++ benchmarks as part
of their platform jobs; the four-language measurements currently run on Linux x64.

Pull requests from branches in this repository also receive one benchmark comment,
updated in place by `marocchino/sticky-pull-request-comment`. It reuses the Actions
summary saved as `summary.md` in the report artifact, with the job status, head and
tested revisions, and a link to the workflow and full reports. A separate job has
`pull-requests: write` permission and only reads the artifact; it does not check out
or run PR code. Runs for an outdated head or a closed PR do not update the comment.

Fork PRs retain the summary and artifacts without attempting to write a comment.
The current tables compare implementations on the same inputs; they do not measure
the PR's change against the base branch. A future regression comparison needs an
explicit base-revision measurement or a stored baseline.
