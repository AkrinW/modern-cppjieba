# 构建与安装

[快速开始](../README.md#快速开始) · [C++ 使用指南](usage.md) · [C ABI](../api/README.md)

## 环境

使用 CMake 3.20+ 和支持 C++23 的编译器、标准库。核心库使用 `<format>`，应用示例还使用 `<print>`。
当前 CI 配置覆盖以下工具链及其 Debug / Release 构建：

| 平台 | CI 工具链 |
| --- | --- |
| Linux x64 / ARM64 | GCC 15 |
| macOS ARM64 | Homebrew LLVM 23 / libc++ |
| Windows x64 | Visual Studio 2026 |

编译器选择和平台运行库参数见 [Linux](../.github/workflows/cmake.yml)、
[macOS / Windows](../.github/workflows/platforms.yml) 和 [C API](../.github/workflows/c-api.yml) 的配置。
下列构建命令从仓库根目录执行；应用消费端命令从应用项目根目录执行。

## 源码接入

将仓库放入应用的 `external/neo-cppjieba`，在应用的 CMakeLists.txt 中链接接口目标：

```cmake
add_subdirectory(external/neo-cppjieba)
target_link_libraries(my_app PRIVATE neo_cppjieba::neo_cppjieba)
```

该目标提供头文件路径、C++23 标准和 MSVC 的 `/utf-8` 选项。
`BUILD_TESTING` 沿用父项目设置；仅接入库时，可在应用的配置命令中传入 `-DBUILD_TESTING=OFF`。

## 安装与消费

```sh
cmake -S . -B build-library -DBUILD_TESTING=OFF
cmake --install build-library --prefix install
```

在应用中使用安装后的包：

```cmake
find_package(neo_cppjieba 1.2 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE neo_cppjieba::neo_cppjieba)
```

配置应用时传入 `-DCMAKE_PREFIX_PATH=/absolute/path/to/install`。默认安装布局如下，
实际目录名遵循 `GNUInstallDirs`：

| 内容 | 安装前缀下的路径 |
| --- | --- |
| C++ 头文件 | `include/neo/` |
| 词典和模型 | `share/neo_cppjieba/dict/` |
| C++ CMake 包 | `lib/cmake/neo_cppjieba/` |
| C API CMake 包（启用时） | `lib/cmake/neo_cppjieba_c/` |

部署时携带词典和模型，并将实际路径传给分词器。`find_package` 之后，
`neo_cppjieba_DICT_DIR` 给出安装包中的词典目录，可用于测试或复制到部署目录。
手工集成时，把 `include/` 加入头文件搜索路径，并启用 C++23 和 MSVC 的 UTF-8 源码选项。

## 可选组件

配置源码项目时启用 `CPPJIEBA_ENABLE_ICU=ON`，并提供 ICU 的 `uc`、`data` 开发库，
即可链接 `neo_cppjieba::icu`。安装消费端通过 `COMPONENTS icu` 选择该组件：

```cmake
find_package(neo_cppjieba 1.2 CONFIG REQUIRED COMPONENTS icu)
target_link_libraries(my_app PRIVATE neo_cppjieba::icu)
```

核心目标使用随库头文件和标准库；ICU 在选择 `icu` 组件时查找。
转换接口及 `ICU_ROOT` 配置见 [编码指南](encoding.md)。

`CPPJIEBA_BUILD_C_API=ON` 增加共享库、静态库和纯 C 头文件。
构建、链接与运行库选择集中在 [C ABI 指南](../api/README.md)。

## 构建选项

| 选项 | 默认值 | 作用 |
| --- | --- | --- |
| `BUILD_TESTING` | 顶层 ON；子项目未预设时 OFF | 构建单元测试 |
| `CPPJIEBA_BUILD_EXAMPLES` | OFF | 构建应用示例 |
| `CPPJIEBA_BUILD_LEGACY_TESTS` | OFF | 增加原版 CppJieba 回归测试，要求 `BUILD_TESTING=ON` |
| `CPPJIEBA_BUILD_BENCHMARKS` | OFF | 构建独立性能程序 |
| `CPPJIEBA_BUILD_RUST_BENCHMARKS` | OFF | 增加 Rust 比较，要求 benchmark 和 Cargo |
| `CPPJIEBA_ENABLE_ICU` | OFF | 增加 ICU 编码转换目标及测试 |
| `CPPJIEBA_BUILD_C_API` | OFF | 构建 C ABI 库 |
| `CPPJIEBA_C_API_BUILD_SHARED` | ON | 启用 C API 后构建共享库 |
| `CPPJIEBA_C_API_BUILD_STATIC` | ON | 启用 C API 后构建静态库 |
| `CPPJIEBA_INSTALL_LEGACY_DICT_DIR` | OFF | 过渡选项：词典同时安装到 1.2 及更早版本的 `share/cppjieba/dict/` |

C API 至少选择一种库形式。测试与比较依赖的版本、初始化和离线配置见 [依赖指南](../deps/README.md)。

## 测试

```sh
cmake -S . -B build-tests -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-tests --config Debug --parallel
ctest --test-dir build-tests -C Debug --output-on-failure
```

GoogleTest 按父项目目标、本地安装、源码下载的顺序查找；使用已有安装时设置 `GTest_DIR` 或 `CMAKE_PREFIX_PATH`。
单元测试覆盖分词、编码、输出契约和容量变体，安装消费端测试检查包导出及可选组件。
启用 ICU 或 C API 后，CTest 增加相应的验证。性能测量命令见 [benchmark 指南](../benchmark/README.md)。

## 仓库布局

| 目录 | 职责 |
| --- | --- |
| `include/neo/` | 一级目录是门面、`Config.hpp` 和 `Fwd.hpp`；按角色划分的二级目录（见 [设计原则·目录](design.md#目录)）及随库第三方头文件 |
| `api/` | C ABI 头文件、适配实现和平台规则 |
| `cmake/` | 项目选项、安装导出和共享测试数据 |
| `deps/` | 测试、比较依赖的配置及子模块 |
| `dict/`、`examples/` | 词典模型、可运行示例 |
| `test/` | 单元测试、消费端测试、测试数据和 CMake 验证脚本 |
| `benchmark/`、`docs/benchmarks/` | 测量程序、报告生成工具及已发布的数据快照 |
| `tools/` | 格式化和静态分析脚本 |

`build*/`、`cmake-build-*/`、`install/` 和 `benchmark/results/` 用于生成输出及本地测量记录。
