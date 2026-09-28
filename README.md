# modern-cppjieba

基于 [CppJieba](https://github.com/yanyiwu/cppjieba) 的 C++23 中文分词库，
以头文件形式提供，使用独立的 `neo_cppjieba` API。

- 支持混合、最大概率、HMM、全模式和搜索分词，可显式关闭 HMM。
- 支持 UTF-8、UTF-16、UTF-32 输入，返回词语及其在原文中的位置。
- 提供借用原文的 token 视图、拥有原文的结果和独立字符串输出。
- 支持复用工作区，减少连续分词时的临时缓冲分配。
- 默认采用 CppJieba 分词规则，也可在编译期选择 Rust / Python 风格。

[应用示例](examples/README.md) · [性能对比](benchmark/README.md) · [更新记录](CHANGELOG.md)

## 快速开始

需要 **CMake 3.20+**，以及支持 C++23 和所用标准库功能（包括 `<format>`、`<print>`）的工具链。
CI 配置使用 Linux GCC 14、macOS LLVM 20 / libc++ 和 Windows Visual Studio 2022；
平台参数见 [Linux 工作流](.github/workflows/cmake.yml) 和 [macOS / Windows 工作流](.github/workflows/platforms.yml)。

克隆项目并构建示例：

```sh
git clone https://github.com/AkrinW/modern-cppjieba.git
cd modern-cppjieba
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DCPPJIEBA_BUILD_EXAMPLES=ON
cmake --build build --config Release --target jieba_basic_cut --parallel
./build/examples/jieba_basic_cut dict "我来到北京清华大学"
```

Visual Studio 生成器的可执行文件位于 `build/examples/Release/jieba_basic_cut.exe`。
以上步骤无需初始化 submodule，也不下载 GoogleTest 或 Rust 依赖。

在自己的程序中，包含 `neo/Jieba.hpp`，并显式传入词典与模型路径：

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

在仓库根目录运行该程序，输出：

```text
我
来到
北京
清华大学
```

词典和 HMM 模型是运行时数据，部署时需一并提供。第三个参数传 `""` 可关闭用户词典；
相对路径以进程工作目录为基准。Windows 的路径参数使用 UTF-8。

## 集成到项目

将本项目放入 `external/modern-cppjieba`，在定义了 `my_app` 的 CMake 项目中加入：

```cmake
add_subdirectory(external/modern-cppjieba)
target_link_libraries(my_app PRIVATE neo_cppjieba::neo_cppjieba)
```

该目标传递头文件路径、C++23 要求和 MSVC 的 `/utf-8` 选项。
仅集成库时，配置父项目可传 `-DBUILD_TESTING=OFF`；若父项目已开启该选项，也会开启本项目单测。

也可以安装头文件和词典：

```sh
cmake -S . -B build-library -DBUILD_TESTING=OFF
cmake --install build-library --prefix install
```

默认安装到 `install/include/neo/` 和 `install/share/cppjieba/dict/`。
当前安装不生成 `find_package` 配置；手工集成时添加 include 路径并启用 C++23。
请保留完整的 `neo/` 目录，其中包含编译所需的内部头文件和随库分发的 GTL。

## 选择分词方式

每次调用都显式指定 `CutMode`：

| 模式 | 用途 |
| --- | --- |
| `MIX` | 词典分词结合 HMM，适合一般文本 |
| `MIX_NO_HMM` / `MP` | 仅使用词典的最大概率路径 |
| `SEARCH` | 在混合分词基础上补充较短词，适合搜索索引 |
| `SEARCH_NO_HMM` | 搜索分词，关闭 HMM |
| `FULL` | 输出词典匹配，结果可能重叠 |
| `HMM` | 单独使用隐马尔可夫模型分词 |

编译期规则由 [Config.hpp](include/neo/Config.hpp) 中的
`compile_config::segmentation_style` 选择：`CPP`（默认）、`RUST` 或 `PYTHON`。
后两者分别参照 jieba-rs 0.11.0 和 jieba 0.42.1 的分词规则，仍使用本库的词典加载接口，
不保证与上游所有行为一致。跨实现对比的方法和限制见 [Rust 对比](benchmark/RUST.md)、
[Python 对比](benchmark/PYTHON.md)。
其中 RUST 的 FULL 可能省略未匹配的汉字，PYTHON 的 FULL 可能产生空 token。

## 选择结果形式

| 入口 | 结果 |
| --- | --- |
| `cut(text, mode)` | token 视图及位置；借用原文 |
| `cut_owned(string, mode)` | 持有原文及位置，可接收移动的字符串 |
| `cut_strings(text, mode)` | 每个词均为独立字符串 |
| `cut_with_workspace(text, mode, workspace)` | 借用原文的结果，复用工作区 |
| `cut_into(text, mode, out, workspace)` | 替换并复用调用方的位置或字符串数组 |
| `cut_each(text, mode, emit, workspace)` | 通过返回 `void` 的回调逐词接收 token |
| `cut_runes(runes, mode)` / `cut_runes_into(runes, mode, out, workspace)` | 处理已解码码点，输出 `WordRange` |

token 提供 `word` 视图与两套半开区间 `[begin, end)`：

- `position.source`：原文编码单元的位置；UTF-8 按字节，UTF-16 / UTF-32 按各自的编码单元计数。
- `position.runes`：解码后 Unicode 码点的位置。

使用借用结果期间，原文必须保持有效且内容稳定；需要保存临时字符串的结果时，使用
`cut_owned` 或 `cut_strings`。拥有原文的结果移动后应重新取得 token 视图。

批量分词可复用 `Workspace` 和输出数组。每个并发或嵌套调用使用独立工作区，
输入也不能引用输出数组的存储。工作区复用或释放不影响已返回的位置数组。
完整用法见 [批量处理、搜索高亮与结果所有权示例](examples/README.md)。

[Config.hpp](include/neo/Config.hpp) 还提供容量类型配置，默认使用 32 位索引和偏移。
修改容量或分词规则后，所有使用本库的翻译单元须采用相同配置并重新编译。
调用方须保证输入和词典规模不超过所选类型的容量；Release 构建不提供运行时容量检查。

## 与 CppJieba 的关系

现代实现位于 `include/neo/`，公共入口见 [Jieba.hpp](include/neo/Jieba.hpp)，
`detail/` 属于内部实现。原版 CppJieba 位于 `deps/cppjieba` submodule，用于回归和性能比较。

现代 API 聚焦分词，与原版接口不兼容；原版的词性标注、关键词提取、运行时增删词接口尚未提供。
仅使用现代库时，无需 CppJieba、limonp、GoogleTest 或 Rust。比较依赖及固定版本见
[deps/README.md](deps/README.md)。

## 开发与测试

启用单元测试时，CMake 会获取 GoogleTest：

```sh
cmake -S . -B build-tests -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-tests --config Debug --parallel
ctest --test-dir build-tests -C Debug --output-on-failure
```

| CMake 选项 | 默认值 | 作用 |
| --- | --- | --- |
| `BUILD_TESTING` | 顶层 ON；子项目未预设时 OFF | 现代实现的单元测试 |
| `CPPJIEBA_BUILD_EXAMPLES` | OFF | 构建应用示例 |
| `CPPJIEBA_BUILD_LEGACY_TESTS` | OFF | 增加原版回归测试，要求启用单元测试 |
| `CPPJIEBA_BUILD_BENCHMARKS` | OFF | 构建独立 benchmark |
| `CPPJIEBA_BUILD_RUST_BENCHMARKS` | OFF | 增加 Rust 比较，要求启用 benchmark |

原版测试和 benchmark 的依赖初始化见 [依赖说明](deps/README.md)，
测量命令与结果解释见 [benchmark/README.md](benchmark/README.md)。
性能数据应注明工具链、输入、词典和分词规则；本项目不以固定性能倍数作为保证。

## 许可证与致谢

采用 [MIT License](LICENSE)。感谢 [CppJieba](https://github.com/yanyiwu/cppjieba)、
[jieba](https://github.com/fxsjy/jieba) 和 [jieba-rs](https://github.com/messense/jieba-rs)。
随库分发的 GTL 保留其 [独立许可证](include/neo/third_party/gtl/LICENSE)。
