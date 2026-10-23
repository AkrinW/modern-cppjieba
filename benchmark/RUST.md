# RustJieba 对比基准

本基准使用固定版本的 jieba-rs 0.11.0，源码提交和依赖锁定方式见 [依赖指南](../deps/README.md)。
Rust 使用 release / opt-level=3，C++ 使用 C++23 / Release。
测量结果、环境信息和原始输出保存在 Git 忽略的 `benchmark/results/`。
最新 CI 快照见 [README 性能对比](../README.md#性能对比)，本页末尾保留带日期的历史测量。

## 比较条件

- C++、Neo 和 Rust 加载同一主词典与外部 HMM 文件，关闭用户词典。
  原版 CppJieba 显式使用 `test/testdata/empty_user.dict.utf8`。
- 先校验输出，再预热和轮换采样顺序，报告中位数、最小值和最大值。
- 输出一致时报告跨实现耗时比；有差异时报告原始时间和词序列差异。
- `FFI copied` 在固定 Rust 版本上重现逐词分配、复制模式，可用于测量适配器的复制成本。
  对应上游接口见 [C API 源码](https://github.com/messense/jieba-rs/blob/3a0d75cf7455e2006330a5b4e94156da272cd7b9/capi/src/lib.rs)。

## 八种计时路径

| 路径 | 返回结果 | 计时内容 |
|---|---|---|
| Old C++ strings | `vector<string>` | 旧版公开 API，包含结果析构 |
| Neo C++ strings | `vector<string>` | Neo 公开 API，包含结果析构 |
| Rust FFI copied → C++ strings | `vector<string>` | 重现上游逐词分配、复制模式，再复制为 C++ 字符串 |
| Rust FFI views → C++ strings | `vector<string>` | FFI 传递借用视图，仅在构造最终 C++ 字符串时复制 |
| Rust native owned strings | `Vec<String>` | 整段循环在 Rust 内执行，包含字符串物化和析构 |
| Rust native borrowed tokens | `Vec<Token>` | Rust 原生借用结果，携带位置及原文视图 |
| Neo borrowed tokens | `Tokens<char>` | `cut` 返回借用原文的位置数组，包含每次调用的分配和结果析构 |
| Neo reused token positions | `vector<TokenPosition>` | `cut_into` 复用调用方输出数组及 `Workspace` 内的解码和分词缓冲 |

输入读取、引擎初始化、正确性校验和预热在计时前完成。Rust 原生路径的输入视图构造和 UTF-8 校验也在计时前完成，模拟 Rust 调用者已经持有 `&str` 的情形。每个样本包含指定轮数的完整语料遍历，分词结果通过优化屏障消费；所有路径均核对 token 总数。

计时前核对 Neo 字符串、借用和复用路径的词序列及位置数组。
`Neo reused` 跨样本保留缓冲容量，逐行清空、解码和填充计时，最终缓冲析构在计时后进行。
借用和复用路径都保留 rune 与原文区间。

按输出契约选择比较对象：拥有字符串的路径包含结果物化和析构；
`Neo borrowed` / `Neo reused` 可与 `Rust native borrowed` 比较借用输出的成本。
各语言的容器和分配策略也计入对应路径的耗时。

## 构建与运行

Rust 对比通过构建选项启用，需要可用的 Rust/Cargo，首次构建会下载锁定的依赖。以下命令在仓库根目录执行：

```sh
git submodule update --init deps/cppjieba deps/limonp deps/jieba-rs
cmake -S . -B build-rust-compare -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DCPPJIEBA_BUILD_BENCHMARKS=ON \
  -DCPPJIEBA_BUILD_RUST_BENCHMARKS=ON
cmake --build build-rust-compare --target cut_compare_benchmark -j2
./build-rust-compare/benchmark/cut_compare_benchmark
```

完整参数为 `主词典 HMM模型 空用户词典参数 语料 每样本轮数 样本数`。默认语料是《围城》，每样本 5 轮、共 7 个样本。Linux 可绑定到允许使用且较空闲的 CPU，以减少迁移干扰：

```sh
taskset -c 2 ./build-rust-compare/benchmark/cut_compare_benchmark \
  dict/jieba.dict.utf8 dict/hmm_model.utf8 '' test/testdata/weicheng.utf8 5 7
```

用户词典参数目前只接受空字符串，避免三方默认词频策略不同。如果要加入其他词，应将明确的词频、词性写入共同主词典后另测。

还可在最后追加 JSON 输出路径。该报告包含四种模式、八条路径的计时、差异计数和逐行 Neo token
的 UTF-8 字节区间；导出在计时外进行，供 [Python 对比基准](PYTHON.md)核对输出。

适配器单元测试覆盖四种分词模式、复制与借用输出一致性、UTF-8/内嵌 NUL/空输入，以及原生计时的轮数和 token 计数：

```sh
cargo test --release --locked --manifest-path deps/rust-jieba/Cargo.toml \
  --target-dir build-rust-compare/deps/rust-jieba/cargo
```

测试也会检查非法 UTF-8 返回错误；C++ 包装在复制发生异常时仍通过 RAII 释放 Rust 结果。


<details>
<summary>2026-09-22 历史测量</summary>

## 2026-09-22 本机复测（目录迁移前记录）

环境：Intel Xeon Gold 5320，Linux，绑定 CPU 2；GCC 16.1.0、rustc 1.94.0。两边均为优化构建，未额外启用 native CPU 指令集或 LTO。表中单位为毫秒，均取 7 个样本的中位数。

### 《围城》：245 个非空行，733,678 UTF-8 字节，每样本遍历 5 轮

| 模式 | Old C++ | Neo C++ | Rust FFI copied | Rust FFI views | Rust native owned | Rust native borrowed | Neo/Rust 不同行数 |
|---|---:|---:|---:|---:|---:|---:|---:|
| MIX | 229.781 | 155.083 | 109.940 | 78.013 | 93.516 | 63.755 | 205/245 |
| MP | 157.834 | 107.739 | 100.035 | 67.499 | 84.349 | 53.033 | 51/245 |
| FULL | 155.026 | 101.278 | 118.667 | 64.856 | 97.977 | 43.405 | 242/245 |
| SEARCH | 255.097 | 158.350 | 122.842 | 87.633 | 105.160 | 71.801 | 205/245 |

Old/Neo 在四种模式下均为 0/245 行差异。Rust 的结果存在语义差别，因此此表提供调用路径耗时，不能视为三方等价工作量排名。可观察的例子包括 MP 模式的连续拉丁字母、数字合并，以及 FULL 模式额外输出的单字。

同一 Rust 版本、同一语料、同样返回 C++ 字符串，仅取消 Rust 侧冗余词文本复制，MIX/MP/FULL/SEARCH 分别提速 **1.41× / 1.48× / 1.83× / 1.40×**。这个对比不受三方分词语义差异影响。

### 现有短句语料：`test/testdata/testlines.utf8`，每样本遍历 10,000 轮

8 行、255 UTF-8 字节。MIX 和 SEARCH 的三方分词输出逐词一致；MP 有 2/8 行 Neo/Rust 差异，FULL 有 8/8 行差异。这里只列输出一致的模式：

| 模式 | Old C++ strings | Neo C++ strings | Rust FFI copied → C++ strings | Rust FFI views → C++ strings | Rust native owned | Rust native borrowed |
|---|---:|---:|---:|---:|---:|---:|
| MIX | 78.759 | 67.827 | 53.124 | 43.002 | 34.004 | 24.775 |
| SEARCH | 97.553 | 68.151 | 62.396 | 49.047 | 45.557 | 31.314 |

在这组输出一致的短句上，Rust FFI views 相对 Neo 的耗时比分别为 1/1.58、1/1.39。此结论仅针对该语料与版本；历史 Rust 源码/构建缺失，不能将历史成绩与本次成绩的全部差距归因于适配器修复。

验证通过：4 个 Rust 单元测试、C++ 四模式接入与短句结果检查、非法轮数和非空用户词典参数拒绝检查，以及关闭 Rust 选项后的普通 CMake 配置。实际 C++ 编译命令使用 `-O3 -DNDEBUG -std=c++23`。

完整输出保存在本地构建目录的 `weicheng-results.txt`、`short-lines-results.txt` 和 `smoke-results.txt` 中；构建目录不纳入版本控制。

</details>
