# modern-cppjieba

基于 CppJieba 的 C++23 中文分词库，以 `include/neo/` 中的头文件提供实现，命名空间为 `neo_cppjieba`。
支持 MIX、MP、HMM、FULL、SEARCH 分词模式。库本身不依赖 old cppjieba、limonp、Rust 或 GoogleTest。

需要 CMake 3.20 以上，以及支持所用 C++23 标准库功能（包括 `<print>`）的编译器。
当前实现使用 POSIX 文件映射接口；CI 使用 Linux 和 [GCC 14](https://gcc.gnu.org/gcc-14/changes.html)。

## 目录与依赖

| 目录 | 用途 |
| --- | --- |
| `include/neo/` | 对外提供的库头文件 |
| `dict/` | 词典与 HMM 模型 |
| `test/unittest_neo/` | neo 功能单元测试 |
| `test/unittest/` | 可选的旧库回归测试 |
| `test/testdata/` | 单测和 benchmark 共用的数据 |
| `benchmark/` | 独立 benchmark 和比较程序，不注册为 CTest 测试 |
| `deps/` | cppjieba 与 limonp submodule、Rust adapter 与锁文件 |
| `tools/` | 格式化与静态分析脚本 |

旧 C++ 比较基线使用官方 cppjieba submodule，固定到具体 commit；limonp 同样由 submodule 锁定。
依赖来源及默认词典路径见 [deps/README.md](deps/README.md)。

## 仅使用库

```sh
git clone https://github.com/AkrinW/modern-cppjieba.git
cd modern-cppjieba
cmake -S . -B build-library -DBUILD_TESTING=OFF
cmake --install build-library --prefix install
```

这条路径不需要初始化 submodule，也不获取 GoogleTest 或 Rust 依赖。
安装内容为 `include/neo/` 和 `share/cppjieba/dict/`；第三方比较库不安装。

也可在自己的 CMake 项目中通过 `add_subdirectory` 引入，链接
`neo_cppjieba::neo_cppjieba`，由该目标传递 include 路径和 C++23 要求。
父项目已启用 `BUILD_TESTING` 时，该选项也会启用本项目单测；仅集成库时需关闭它。

```cmake
add_subdirectory(path/to/modern-cppjieba)
target_link_libraries(my_app PRIVATE neo_cppjieba::neo_cppjieba)
```

在仓库根目录运行时的使用示例：

```cpp
#include "neo/Jieba.hpp"

#include <print>

// Segment text with explicit dictionary and model paths.
int main() {
    const auto jieba = neo_cppjieba::Jieba{
        "dict/jieba.dict.utf8", "dict/hmm_model.utf8", "dict/user.dict.utf8"};
    for (const auto token : jieba.cut("我来到北京清华大学", neo_cppjieba::CutMode::MIX)) {
        std::println("{}", token.word);
    }
}
```

`cut(text, mode)` 返回借用原文的 token，每个 token 提供 `word` 视图及
`position.runes`、`position.source` 两套半开区间。源区间以输入的编码单元计数：UTF-8 为字节，
UTF-16 为 `char16_t` 单元，宽字符串为 `wchar_t` 单元。rune 区间始终按 Unicode 码点计数。
原文需要在使用视图期间保持有效且稳定；接口拒绝直接借用临时 owning string，但无法检查显式构造的悬垂 `string_view`。

| 入口 | 输出与所有权 |
| --- | --- |
| `cut(text, mode)` | 自有位置数组，借用原文 |
| `cut_into(text, mode, out, decoded)` | 替换并复用调用方的源区间、token 位置或字符串数组 |
| `cut_each(text, mode, emit, decoded)` | 调用返回 `void` 的 visitor，逐词交付 `TokenView` |
| `cut_owned(text, mode)` | 持有整段原文和位置数组；传入 string 移动值可转移其存储 |
| `cut_strings(text, mode)` | 每个词都有独立的字符串存储 |
| `cut_runes(runes, mode)` / `cut_runes_into(runes, mode, out)` | 合法预解码输入，返回或填写 `WordRange` |

`CutMode` 显式区分 `MIX`、`MIX_NO_HMM`、`MP`、`FULL`、`SEARCH`、`SEARCH_NO_HMM`、`HMM`。
Unicode 解码和编码使用 `Unicode.hpp` 的自由函数。已有 `CutMethod` 模板入口和 Jieba 静态编码包装已移除。

`Token.hpp` 定义 token 区间、视图及结果容器；`Unicode.hpp` 负责解码；`Jieba.hpp` 负责模式选择和输出转换。
高频调用直接复用 `UnicodeWithOffset decoded`，其中的 `runes` 和 `offsets` 各自保留容量，
需要释放内存时赋值为 `UnicodeWithOffset{}`。`decode_with_offset_into(text, decoded)` 也使用同一套校验与缓冲复用逻辑。

输入不能借用输出数组或解码缓冲的存储。同一份解码缓冲须由调用方独占使用，visitor 内嵌套分词应使用另一份缓冲；
这里没有内部锁或重入状态。visitor 异常直接传播，已执行的副作用不回滚，解码缓冲仍可用于下一次调用。

输出数组独立于解码缓冲。现有分词器仍先生成内部 rune 区间，`cut_each` 随后逐词调用 visitor；
DAG、DP、HMM 的内部缓冲尚未复用。拥有原文的结果通过偏移访问文本，移动结果后应重新取得视图。

## 单元测试

默认只构建 neo 单测；GoogleTest 在启用单测时获取。

```sh
cmake -S . -B build-tests -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-tests --parallel
ctest --test-dir build-tests --output-on-failure
```

如需同时验证旧库：

```sh
git submodule update --init deps/cppjieba
cmake -S . -B build-tests -DBUILD_TESTING=ON -DCPPJIEBA_BUILD_LEGACY_TESTS=ON
cmake --build build-tests --parallel
ctest --test-dir build-tests --output-on-failure
```

CTest 名称为 `neo_unit_tests` 和可选的 `legacy_unit_tests`。
单元测试不运行计时比较，也不按性能快慢判定通过。

## Benchmark

```sh
git submodule update --init deps/cppjieba deps/limonp
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DCPPJIEBA_BUILD_BENCHMARKS=ON
cmake --build build-bench --parallel
./build-bench/benchmark/jieba_benchmark
```

benchmark 可以独立构建，不下载或链接 GoogleTest。需要 Rust 比较时，再设置
`-DCPPJIEBA_BUILD_RUST_BENCHMARKS=ON`；该开关要求同时开启 benchmark。
目标列表见 [benchmark/README.md](benchmark/README.md)，Rust 构建与结果解释见
[benchmark/RUST.md](benchmark/RUST.md)。

## 构建选项

| 选项 | 默认值 | 作用 |
| --- | --- | --- |
| `BUILD_TESTING` | 顶层项目 ON，子项目未预设时 OFF | neo 单测 |
| `CPPJIEBA_BUILD_LEGACY_TESTS` | OFF | 附加旧库单测，要求 `BUILD_TESTING=ON` |
| `CPPJIEBA_BUILD_BENCHMARKS` | OFF | 独立 benchmark |
| `CPPJIEBA_BUILD_RUST_BENCHMARKS` | OFF | 附加 Rust 比较，要求 benchmark 开启 |

优化级别由 `CMAKE_BUILD_TYPE` 控制；性能比较使用 `Release`。
格式化和静态分析默认扫描 `include/neo/`、`test/`、`benchmark/`，跳过 `deps/`。

旧版接口与历史文档保存在 [deps/cppjieba-upstream-history.md](deps/cppjieba-upstream-history.md)。
