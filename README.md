# modern-cppjieba

基于 CppJieba 的 C++23 中文分词库，以 `include/neo/` 中的头文件提供实现，命名空间为 `neo_cppjieba`。
支持 MIX、MP、HMM、FULL、SEARCH 分词模式。库本身不依赖 old cppjieba、limonp、Rust 或 GoogleTest。

需要 CMake 3.20 以上，以及支持所用 C++23 标准库功能（包括 `<print>`）的编译器。
当前实现使用 POSIX 文件映射接口；CI 使用 Linux 和 [GCC 14](https://gcc.gnu.org/gcc-14/changes.html)。

## 目录与依赖

| 目录 | 用途 |
| --- | --- |
| `include/neo/` | 用户直接使用的公开头文件 |
| `include/neo/detail/` | 分词算法、词典与模型、Trie/DAG、文件读取及诊断实现 |
| `include/neo/third_party/` | 随库分发的 GTL 头文件及许可证 |
| `dict/` | 词典与 HMM 模型 |
| `test/unittest_neo/` | neo 功能单元测试 |
| `test/unittest/` | 可选的旧库回归测试 |
| `test/testdata/` | 单测和 benchmark 共用的数据 |
| `benchmark/` | 独立 benchmark 和比较程序，不注册为 CTest 测试 |
| `examples/` | 可运行的应用示例：模式对比、批量处理、搜索高亮、词频与结果所有权 |
| `deps/` | cppjieba 与 limonp submodule、Rust adapter 与锁文件 |
| `tools/` | 格式化与静态分析脚本 |

公开头文件按用途划分：

| 头文件 | 用途 |
| --- | --- |
| `neo/Jieba.hpp` | 分词入口与 `CutMode` |
| `neo/Token.hpp` | token 视图、位置、借用与拥有结果 |
| `neo/TokenView.hpp` | 仅使用 token 视图与位置类型时的入口 |
| `neo/Unicode.hpp` | Unicode 类型、编解码、可复用解码缓冲 |
| `neo/UnicodeTypes.hpp` | `Rune`、`Unicode`、`UnicodeWithOffset`、`WordRange` 类型 |
| `neo/Workspace.hpp` | 调用方独占的解码与分词工作区 |
| `neo/Traits.hpp` | 输入类型约束、编码识别及无拷贝输入适配 |
| `neo/Config.hpp` | 编译期容量类型、日志配置与异常类型 |

编解码操作统一包含 `neo/Unicode.hpp`，其中也提供 `WordRange::to_string<CharT>()` 的定义。
只使用 Unicode 数据类型和范围操作时，可包含 `neo/UnicodeTypes.hpp`。

`detail/` 是内部实现边界，供库代码、内部测试与基准直接引用。词典和 HMM 模型由 `Jieba` 私有持有，
公开入口不再提供 `dict()`、`model()`。实现头的旧路径已移除。
头文件库仍需安装 `detail/` 和 `third_party/`，以满足公开头的编译依赖。

容量类型统一定义在 `include/neo/Config.hpp`。修改其中的 `using` 后，需要使用相同配置重新编译所有
包含本库的代码；同一程序的不同翻译单元不能混用配置。这是编译期配置。

四种类型默认均为 `std::uint32_t`，可独立选择 8、16、32、64、128 位。选择时需要分别估算以下业务负载：

| 配置项 | 需要估算的输入 | 对业务的影响 |
| --- | --- | --- |
| `RuneIndex` | 一次调用中解码后的 Unicode 码点总数，包括分隔符 | 限制整个输入的长度；`cut_runes` 也受此限制 |
| `SourceOffset` | 一次调用中原字符串的长度，单位随编码变化 | UTF-8 按字节，UTF-16/UTF-32 按各自的 code unit 计数 |
| `DagOffset` | 内部 DAG 对照工具的单字边数，加上所有多字词命中次数 | 仅约束显式构造 DAG 的内部测试和基准；所有分词模式均不使用 DAG |
| `TrieNodeId` | 主词典和用户词典合并后的词条数、词长及前缀共享情况 | 决定能装入什么样的词典；不能直接换算成固定的文件 MB 上限 |

**输入字符串长度。** 下表假设 `RuneIndex`、`SourceOffset` 选择同一档位，输入为 UTF-8。
“整次输入”两列给出编码长度允许的预算，还必须满足码点数和可用内存约束。
码点按解码后的 Unicode 计数；一个可见字符可能包含多个码点。

| 类型（单个值占用） | 整次 UTF-8 输入的字节预算 | 按该预算可放入的纯文本示例 |
| --- | --- | --- |
| `std::uint8_t`（1 字节） | 255 字节 | 255 个 ASCII / 85 个三字节汉字 / 63 个四字节码点 |
| `std::uint16_t`（2 字节） | 64 KiB − 1 字节 | 65,535 个 ASCII / 21,845 个三字节汉字 / 16,383 个四字节码点 |
| `std::uint32_t`（4 字节） | 4 GiB − 1 字节 | 4,294,967,295 个 ASCII / 1,431,655,765 个三字节汉字 / 1,073,741,823 个四字节码点 |
| `std::uint64_t`（8 字节） | 常见 64 位平台先受容器和内存限制 | 需按原文、解码缓冲、各模式临时数据和输出的总内存估算 |
| `neo_cppjieba::uint128_t`（16 字节） | 同一 64 位平台的可装载字符串不会因此变长 | 更宽的偏移和范围会增加缓冲开销 |

FULL 直接输出多字词匹配，并为未被覆盖的码点输出单字；匹配越密集，结果数组所需内存越大。
所有分词模式均不受 DAG 边数上限限制。`DagOffset` 仅保留给内部 DAG 对照工具：长度为 `n` 的输入
需要 `n` 条单字边和所有多字词匹配边，最密集时共 `n × (n + 1) / 2` 条；8/16/32 位分别容纳
22/361/92,681 个码点的这种密集输入。该限制不再约束 FULL 的结果数量。

空格、Tab、换行、`，`、`。` 会把分词输入拆成独立片段；这些分隔符仍计入整个输入的字节数和码点数。
`cut_runes` 接收已解码码点，不受 `SourceOffset` 限制；其他分词入口仍需同时满足原文长度和码点数限制。
UTF-16 输入中的常见汉字每字占一个 code unit，补充平面码点占两个；UTF-32 每个码点占一个。
配置位宽不同或编码不同时，应分别核算，不能直接套用 UTF-8 表格。

**词典规模。** 当前随库的 `dict/jieba.dict.utf8` 为 5,071,189 字节（约 4.84 MiB），含 348,981 条词，
最长词 16 个码点；将所有词的不同非空真前缀去重并加上根节点，共需 201,007 个节点。
因此该词典需要至少 32 位 `TrieNodeId`。这些数据仅针对当前主词典文件，不含用户词典和 HMM 模型。

词典文件大小本身没有由这些类型规定的 MB 上限。长词和较少的前缀共享会增加节点消耗；
词频、词性字段会增加文件字节数，却不会增加节点数。实际应按合并后的词条内容估算，并留出加载和存储所需内存。
对于 `W` 条词、每条至多 `L` 个码点（`L >= 1`），节点数不超过 `1 + W × (L − 1)`；
这个估计不依赖前缀共享，单字词不增加根节点以外的节点。`b` 位节点配置需满足该估计不超过 `2^b − 1`，
或对实际前缀去重后计算更准确的节点数。

| `TrieNodeId` | 每词至多 4 个码点时，节点容量保守可容纳的词条数 | 当前随库完整主词典 |
| --- | --- | --- |
| `std::uint8_t` | 84 条 | 容纳不下 |
| `std::uint16_t` | 21,844 条 | 容纳不下 |
| `std::uint32_t` | 1,431,655,764 条；实际还需足够内存 | 可容纳 |
| `std::uint64_t` | 常见 64 位平台先受容器和内存限制，需按词条内容估算 | 可容纳 |
| `neo_cppjieba::uint128_t` | 同一平台的可装载词典不会因此扩大，记录更宽会增加内存开销 | 可容纳 |

表中的词条数是满足词长条件时的保守额度。短词更多、前缀共享更多的词典可以容纳更多条目，
例如 8 位节点配置能容纳任意 254 条双字词；不能把“84 条四字以内词”当作所有词典的统一条数上限。
通常可保留词典节点为 32 位，仅根据单次请求大小缩小文本容量；三种文本配置为 8 位并不要求词典也使用 8 位。

C++23 没有 `std::uint128_t`；库在编译器定义 `__SIZEOF_INT128__` 的目标上提供
`neo_cppjieba::uint128_t`，对应原生 `unsigned __int128` 扩展，参见
[GCC 的 128 位整数说明](https://gcc.gnu.org/onlinedocs/gcc/_005f_005fint128.html)。不支持该扩展的目标仍可使用其余四档。
选择 128 位不会扩大宿主的地址空间；实际容器访问仍使用 `size_t`。

节点位宽也影响 Trie 的转移键和边记录大小，常见 64 位平台上的布局如下；具体以目标平台的 `sizeof` 为准。

| `TrieNodeId` 位宽 | 每个转移键 | 每个边记录（节点 ID、过滤掩码、词典值） |
| --- | --- | --- |
| 8 / 16 / 32 | 8 字节 | 16 字节 |
| 64 | 16 字节 | 24 字节 |
| 128 | 32 字节 | 32 字节 |

哈希槽还包含对齐填充和控制字节；根表直接保存边记录。默认 32 位配置保持原来的紧凑布局。

所有负载还受 `size_t`、容器 `max_size()` 和可用内存限制。偏移表需要一个结尾哨兵，
会分配 `rune 数量 + 1` 个元素；解码还按 `源 code-unit 数量 + 1` 预留偏移容量，相关加法必须能由 `size_t` 表示。
每个 `WordRange` 保存两个 `RuneIndex`，每个 `SourceRange` 保存两个 `SourceOffset`。
内部对照工具中，将 `DagOffset` 从 32 位改成 64 位，会让每份 DAG 的偏移表增加 `4 × (输入 rune 数量 + 1)` 字节。
DAG 边本身保存一个 `RuneIndex` 和一个 `float`；常见 64 位平台上，32/64 位 `RuneIndex` 对应每条边 8/16 字节。
8/16 位字段仍可能因对齐占用相同大小；128 位 `RuneIndex` 的边通常占 32 字节，具体以 `sizeof(DagEdge)` 为准。

调用方负责选择足够的容量类型，并保证文本、生成的 DAG 和词典节点不超限。容量前置条件由
`assert` 或 `assert_check` 在 Debug 构建中断言；定义 `NDEBUG` 后不进行运行时容量检查，超限违反调用契约，
可能导致截断、越界或未定义行为。标准容器仍可能报告分配失败。
非法 Unicode、调用方提供的无效范围、词典或模型格式错误、I/O 错误仍通过原有运行时异常路径报告。

容器长度、分配字节数、统计数量与迭代器差值继续使用 `size_t`/`ptrdiff_t`；
Unicode 码点、编码单元、HMM 状态和 Trie 位掩码使用各自固定表示，不作为容量配置项。

完整回归套件使用 32 位容量，以容纳现有的大输入 fixture。容量测试为 8/16/32/64 位和混合位宽
分别生成独立可执行文件；支持原生 128 位的编译器还会启用
128 位及其混合配置。Trie 另用小词典覆盖全部节点位宽、8 位节点端点，以及 64/128 位 key 的高位区分。
测试使用受控的小输入覆盖窄类型端点和宽类型转换，避免混用不同配置的翻译单元。

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

完整应用示例与构建、运行命令见 [examples/README.md](examples/README.md)。
通过 `-DCPPJIEBA_BUILD_EXAMPLES=ON -DBUILD_TESTING=OFF` 可单独构建示例，无需比较库或测试依赖。

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
| `cut_into(text, mode, out, workspace)` | 替换并复用调用方的源区间、token 位置或字符串数组 |
| `cut_each(text, mode, emit, workspace)` | 调用返回 `void` 的 visitor，逐词交付 `TokenView` |
| `cut_owned(text, mode)` | 持有整段原文和位置数组；传入 string 移动值可转移其存储 |
| `cut_strings(text, mode)` | 每个词都有独立的字符串存储 |
| `cut_runes(runes, mode)` / `cut_runes_into(runes, mode, out, workspace)` | 合法预解码输入，返回或填写 `WordRange` |

`CutMode` 显式区分 `MIX`、`MIX_NO_HMM`、`MP`、`FULL`、`SEARCH`、`SEARCH_NO_HMM`、`HMM`。
Unicode 解码和编码使用 `Unicode.hpp` 的自由函数。已有 `CutMethod` 模板入口和 Jieba 静态编码包装已移除。

`TokenView.hpp` 定义 `SourceRange`、`TokenPosition` 和 `TokenView<CharT>`；`Token.hpp` 包含这些类型，
并提供借用原文的 `Tokens<CharT>` 和持有原文的 `OwnedTokens<CharT>`。两类结果均支持 `size()`、`empty()`、
下标与遍历，以及 `source()`、`positions()` 视图访问。`Unicode.hpp` 负责解码；`Jieba.hpp` 提供分词入口。
高频调用使用 `Workspace workspace`，复用解码、源偏移、分隔位置、MP 路由、HMM 路径、SEARCH 短词标记和内部结果存储。
所有分词模式共用该工作区；同一输入的分隔段之间及后续调用之间均保留容量，`workspace.release()` 显式释放。
`UnicodeWithOffset` 仍用于独立编解码，`decode_with_offset_into(text, decoded)` 的接口保持不变。

输入不能借用输出数组的存储。同一份工作区须由调用方独占使用，visitor 内嵌套分词应使用另一份工作区；
`Jieba` 只持有只读词典与模型，没有线程局部缓存或内部锁。visitor 异常直接传播，已执行的副作用不回滚，工作区仍可继续使用。

输出数组独立于工作区，复用、移动或释放工作区不影响已经返回的位置。`cut_runes_into` 直接填写调用方数组，
其余分词器仍先在工作区生成 rune 区间，`cut_each` 随后逐词调用 visitor。拥有原文的结果移动后应重新取得视图。
`cut_runes_into` 在入口清空输出；分词中途发生异常时直接向上传播，输出可能保留部分结果，调用方应丢弃本次输出。

```cpp
neo_cppjieba::Workspace workspace;
std::vector<neo_cppjieba::TokenPosition> positions;
jieba.cut_into("中国科学院", neo_cppjieba::CutMode::SEARCH, positions, workspace);
```

需要返回独立的 `Tokens` 时，可通过 `Jieba::cut_with_workspace` 显式复用工作区：

```cpp
neo_cppjieba::Workspace workspace;
const auto first = jieba.cut_with_workspace("中国科学院", neo_cppjieba::CutMode::SEARCH, workspace);
const auto second = jieba.cut_with_workspace("北京", neo_cppjieba::CutMode::MIX, workspace);
```

`Jieba` 直接持有词典和 HMM 模型，所有分词操作均由 `Jieba` 提供。
`cut(text, mode)` 使用本次调用的局部工作区；`cut_with_workspace`、`cut_into`、`cut_each`
由调用方显式传入工作区，也可以传入用户层的 `thread_local Workspace`。
同一工作区同一时刻只能供一次调用使用，嵌套分词或并发调用使用不同工作区。
工作区复用、`release()` 或销毁不影响已返回的 `Tokens`；原文仍须保持有效且稳定。

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
