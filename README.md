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
    for (const auto &word : jieba.cut<neo_cppjieba::CutMethod::MIX>("我来到北京清华大学")) {
        std::println("{}", word);
    }
}
```

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
