# Python / C++ / Rust 分词对比

`python_compare_benchmark.py` 使用 `deps/python-jieba` 中的官方
[jieba v0.42.1](https://github.com/fxsjy/jieba/tree/v0.42.1)，与旧 C++、Neo C++、
jieba-rs 0.11.0 对比。Python 源码通过子模块固定，无需 pip 安装，也不会成为 C++ 库的依赖。

## 构建和运行

需要 Python 3.11+、C++23 编译器和 Rust/Cargo。在仓库根目录执行：

```sh
git submodule update --init deps/cppjieba deps/limonp deps/jieba-rs deps/python-jieba
cmake -S . -B build-compare -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DCPPJIEBA_BUILD_BENCHMARKS=ON \
  -DCPPJIEBA_BUILD_RUST_BENCHMARKS=ON
cmake --build build-compare --target cut_compare_benchmark -j2

python3 -B benchmark/python_compare_benchmark.py build-compare/benchmark/cut_compare_benchmark \
  --text test/testdata/weicheng.utf8 --rounds 5 --samples 7 \
  --output benchmark/results/python/weicheng.json
python3 -B benchmark/python_compare_benchmark.py build-compare/benchmark/cut_compare_benchmark \
  --text test/testdata/testlines.utf8 --rounds 10000 --samples 7 \
  --output benchmark/results/python/short-lines.json
```

Linux 可在 `python3` 前加 `taskset -c 2`；原生子进程会继承 CPU 亲和性。
选择当前机器允许使用、较空闲的 CPU。运行期间避免其他编译或性能测试。

`--dict` 和 `--model` 默认指向仓库 `dict/jieba.dict.utf8` 与 `dict/hmm_model.utf8`。
四方只加载这份主词典，不加载用户词典。Python 的内置 HMM 发射概率精度与这份文本模型存在差异，
适配器因此在初始化时把外部模型的 B/E/M/S 概率装入 `jieba.finalseg`，不改动上游算法或源码。
该适配仅用于独立基准进程；模型概率是 Python jieba 的模块级状态。

## 模式和计时口径

| 模式 | Python 接口 | 输出 |
|---|---|---|
| MIX | `Tokenizer.lcut(cut_all=False, HMM=True, use_paddle=False)` | 精确分词，启用 HMM |
| MP | `Tokenizer.lcut(cut_all=False, HMM=False, use_paddle=False)` | 精确分词，关闭 HMM |
| FULL | `Tokenizer.lcut(cut_all=True, HMM=False, use_paddle=False)` | 全模式 |
| SEARCH | `Tokenizer.lcut_for_search(HMM=True)` | 搜索模式 |

Python 每次调用均生成完整 `list[str]`，计时包含分词、结果物化和逐行释放。
每条路径先预热；Python 样本轮换四种模式的顺序，原生样本按既有方式轮换八种输出路径。
报告中位数、最小值和最大值，每个计时样本核对 token 总数。

语料读取、引擎加载、正确性校验、预热、跨进程启动和 JSON 导出均不计时。
Python 持有已经解码的 `str`，UTF-8 解码在计时前完成；C++ 接收 UTF-8 字符串，
Rust 接收已验证的 UTF-8 `&str`。这些是各语言自然接口的成本，不能将差异全部归因于算法。
借用 token、复用位置数组和拥有字符串的成本不同，原生路径的详细定义见 [RUST.md](RUST.md)。

加载语料时仅以 LF 分行、去掉行末一个 CR、跳过空行，保留其他空白、NUL 和 Unicode 内容，
与原生基准一致。脚本按原始 UTF-8 字节区间还原 Neo 输出，与 Python 的完整词序列逐行比较，
保留前两个差异样例。只有 Python/Neo 输出一致时才给出两者字符串路径的耗时比。
即使主词典和模型相同，字符过滤、词频累计、HMM 和全模式规则也可能让不同实现产生不同结果。

## 报告和验证

指定 `--output path.json` 后生成：

- `path.json`：Python/原生计时、差异计数及样例、版本、CPU 亲和性、输入及原生程序 SHA-256。
- `path.native.json`：原生计时及逐行 Neo UTF-8 字节区间。
- `path.native.log`：原生基准完整输出，包括 HMM-only 附录。

所有测量结果放在 Git 忽略的 `benchmark/results/`，不提交。脚本不输出跨语料的汇总速度排名。
运行适配器测试：

```sh
python3 -B -m unittest discover -s benchmark -p 'python_compare_benchmark_test.py' -v
```

测试覆盖模式映射、共享模型、空输入、未知 Unicode、NUL、CRLF、字节区间、
重叠词序列及差异检测。四方实际运行还会检查样本参数、语料大小和所有计时路径的 token 数量。
