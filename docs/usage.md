# C++ 使用指南

[快速开始](../README.md#快速开始) · [构建与安装](building.md) · [应用示例](../examples/README.md)

公共入口是 `neo/Jieba.hpp` 中的 `neo_cppjieba::Jieba`。
构造时依次提供主词典、HMM 模型和用户词典路径；用户词典参数 `""` 表示仅使用主词典。
路径相对于进程工作目录解析，Windows 路径使用 UTF-8。文件格式见 [词典说明](../dict/README.md)。
从内存加载和多个用户词典见 [从内存加载词典](#从内存加载词典)。

## 输入文本

| 字符类型 | 编码 |
| --- | --- |
| `char`、`char8_t`、字节缓冲 | UTF-8 |
| `char16_t` | UTF-16 |
| `char32_t` | UTF-32 |
| `wchar_t` | 按平台宽度使用 UTF-16 或 UTF-32 |

字符串、字符串视图、字符数组和连续范围都携带长度。字符串字面量省略末尾的终止 NUL，
保留内嵌 NUL；视图和范围按给定长度读取。借用型入口要求输入在结果使用期间保持有效。

从 C 接口或其他缓冲区获得字符指针时，使用已知长度构造视图。以下片段中的
`buffer`、`byte_count` 来自调用方，`jieba` 是已创建的分词器：

```cpp
#include <span>
#include <string_view>

const auto text = std::string_view{buffer, byte_count};
const auto tokens = jieba.cut(text, neo_cppjieba::CutMode::MIX);
const auto words = jieba.cut_strings(std::span<const char>{buffer, byte_count}, neo_cppjieba::CutMode::MIX);
```

这也是从 1.1.x 原始字符指针调用迁移到 1.2.0 的方式。
GBK / GB18030 输入先通过 [编码适配器](encoding.md) 转换为 UTF-8。

## 分词模式

| `CutMode` | 说明 |
| --- | --- |
| `MIX` | 词典最大概率路径结合 HMM，识别未登录词 |
| `MIX_NO_HMM` / `MP` | 词典最大概率分词 |
| `SEARCH` | 在混合分词基础上补充较短词，适合搜索索引 |
| `SEARCH_NO_HMM` | 基于词典分词补充搜索词，关闭 HMM |
| `FULL` | 输出词典匹配，结果可以重叠 |
| `HMM` | 使用隐马尔可夫模型分词 |

`Config.hpp` 中的 `compile_config::segmentation_style` 可选 `CPP`、`RUST`、`PYTHON`，默认 `CPP`。
三套规则分别参考 CppJieba、jieba-rs 0.11.0 和 jieba 0.42.1；词典加载及用户词典采用本库的接口约定。
实现间的输出比较见 [Rust 基准](../benchmark/RUST.md) 和 [Python 基准](../benchmark/PYTHON.md)。

## 选择输出形式

| 入口 | 结果和适用场景 |
| --- | --- |
| `cut(text, mode)` | 借用原文的 token 视图及位置，适合即时消费 |
| `cut_owned(string, mode)` | 持有原文和位置，适合保存结果或接收临时字符串 |
| `cut_strings(text, mode)` | 每个词独立存储的字符串数组 |
| `cut_with_workspace(text, mode, workspace)` | 复用工作区，返回借用原文的结果 |
| `cut_into(text, mode, out, workspace)` | 复用调用方的位置数组或字符串数组 |
| `cut_each(text, mode, emit, workspace)` | 通过返回 `void` 的回调逐词接收 token |
| `cut_runes(runes, mode)` / `cut_runes_into(runes, mode, out, workspace)` | 对已校验的 Unicode 标量分词，输出 `WordRange` |

`token.word` 借用原文；在原文修改或销毁前完成读取。`cut_owned` 的结果移动后，重新获取 token 视图。
需要让每个词独立存活时，选择 `cut_strings`。完整的所有权示例见 [owned_results.cpp](../examples/owned_results.cpp)。

## 词性标注

`cut`、`cut_with_workspace`、`cut_owned`、`cut_strings` 和 `cut_each` 接受模板参数 `Tagging`，
默认 `Tagging::None`。传入 `Tagging::Pos` 后，分词结果与同一模式不变，每个词附加 `PosTag`：

| 入口 | 带词性的结果 |
| --- | --- |
| `cut<Tagging::Pos>(text, mode)` / `cut_with_workspace<Tagging::Pos>(...)` | 借用原文的 `TaggedTokenView`，含 `word`、`position`、`tag` |
| `cut_owned<Tagging::Pos>(string, mode)` | 持有原文的带词性结果 |
| `cut_strings<Tagging::Pos>(text, mode)` | `TaggedWord` 数组，每项含独立的 `word` 和 `tag` |
| `cut_each<Tagging::Pos>(text, mode, emit, workspace)` | 回调逐词接收 `TaggedTokenView` |
| `cut_into(text, mode, out, workspace)` | 由输出类型决定：`std::vector<TaggedPosition>` 或 `std::vector<TaggedWord<CharT>>` |
| `lookup_tag(word)` | 查询单个完整词的词性，不做分词；也接受 `std::span<const Rune>` |

返回类型 `Tokens<CharT, Tagging::Pos>` 也可写作 `TaggedTokens<CharT>`。

```cpp
using neo_cppjieba::CutMode, neo_cppjieba::Tagging;
for (const auto token : jieba.cut<Tagging::Pos>("我来到北京清华大学，使用iPhone6", CutMode::MIX)) {
    std::println("{}/{}", token.word, token.tag.to_string_view());
}
// 我/r 来到/v 北京/ns 清华大学/nt ，/x 使用/v iPhone6/eng
```

词性取自主词典或用户词典的第三列。词典中没有该词，或用户词典未给出词性时，沿用 CppJieba 的规则：
从词首开始统计 ASCII 字符，统计数达到词长一半即停止；没有 ASCII 为 `x`，全为数字为 `m`，否则为 `eng`。
因此单字词总是 `x`，`3.14` 为 `eng`。词性规则不受 `segmentation_style` 影响；
Python jieba 的 `posseg` 使用独立的词性 HMM 模型，本库不提供该模型。

`PosTag` 最多 4 个 ASCII 字符，按值存储，可与字符串或 `neo_cppjieba::pos` 中的常量比较，
例如 `token.tag == neo_cppjieba::pos::ns` 或 `token.tag == "ns"`。缩写含义见 [词性标记表](pos_tags.md)。

| CppJieba / Python jieba | neo-cppjieba |
| --- | --- |
| `jieba.Tag(s, words)` | `jieba.cut_strings<Tagging::Pos>(s, CutMode::MIX)` |
| `jieba.LookupTag(word)` | `jieba.lookup_tag(word)` |
| `jieba.posseg.lcut(s)` / `lcut(s, HMM=False)` | `cut_strings<Tagging::Pos>(s, CutMode::MIX)` / `CutMode::MIX_NO_HMM` |

词性只在 `Tagging::Pos` 实例中查询；默认的 `Tagging::None` 不做任何词性相关的工作。

## 关键词提取

可选头文件 `neo/KeywordExtractor.hpp` 提供两种提取器，二者借用已构造的 `Jieba`，`Jieba` 须比提取器活得更久：

| 提取器 | 打分方式 | 需要的文件 |
| --- | --- | --- |
| `TfIdfExtractor(jieba, idf_path, stop_words_path)` | 词在本文中的出现次数 × 该词的 IDF；IDF 文件中没有的词取文件内 IDF 的平均值 | [`idf.utf8`](../dict/idf.utf8)、[`stop_words.utf8`](../dict/stop_words.utf8) |
| `TextRankExtractor(jieba, stop_words_path, options)` | 在窗口内共现的词之间连边，按类似 PageRank 的迭代打分，最高分归一为 1 | [`stop_words.utf8`](../dict/stop_words.utf8) |

两者先用 `CutMode::MIX` 分词，丢弃单字词和停用词，再对剩下的不同词打分，按分数从高到低取前 `top_n` 个；
分数相同时先出现的词在前。结果是 `std::vector<Keyword<CharT>>`，每项含独立的 `word`、`weight`、
`tag`（与 `lookup_tag` 相同）和该词每次出现的 `positions`（坐标与分词结果一致）。

```cpp
#include "neo/KeywordExtractor.hpp"

const auto extractor = neo_cppjieba::TfIdfExtractor{jieba, "dict/idf.utf8", "dict/stop_words.utf8"};
for (const auto &keyword : extractor.extract("我是拖拉机学院手扶拖拉机专业的。不用多久，我就会升职加薪，当上CEO，走上人生巅峰。", 5)) {
    std::println("{} {:.4f} @{}", keyword.word, keyword.weight, keyword.positions.front().source.begin);
}
// CEO 11.7392 @93 / 升职 10.8562 @72 / 加薪 10.6426 @78 / 手扶拖拉机 10.0089 @21 / 巅峰 9.4940 @111
```

### 按词性过滤

`TfIdfOptions` 和 `TextRankOptions` 的 `allowed_tags` 对应 Python jieba 的 `allowPOS`：非空时，
只有词性在列表中的词才成为候选词。默认为空，不过滤，与 CppJieba 一致。
`python_textrank_tags`（`ns`、`n`、`vn`、`v`）是 Python jieba `textrank` 的默认值。

```cpp
using namespace neo_cppjieba;
const auto nouns = TfIdfExtractor{jieba, "dict/idf.utf8", "dict/stop_words.utf8",
                                  {.allowed_tags = {pos::n, pos::nz, pos::vn}}};
// 手扶拖拉机/n 巅峰/n 拖拉机/n 人生/n 学院/n

const auto textrank = TextRankExtractor{
    jieba, "dict/stop_words.utf8",
    {.allowed_tags = {python_textrank_tags.begin(), python_textrank_tags.end()}}};
```

TF-IDF 对每个词独立打分，过滤只删去不符合的词，其余分数不变。TextRank 中被过滤的词与停用词一样
不进入词图，也不占窗口位置，剩下的词之间重新打分。
词性来自词典，而非 Python jieba `posseg` 的词性模型，例如主词典把"加薪"标为 `nr`，
因此过滤结果与 Python jieba 不完全相同。

连续处理文本时，用 `extract_into(text, top_n, out, keyword_workspace)` 复用 `KeywordWorkspace` 和输出数组；
同一工作区可在两种提取器之间共用。`TextRankOptions` 的 `window`、`iterations`、`damping`
默认为 5、10、0.85，对应 CppJieba `Extract` 的 `span`、`rankTime` 和固定的阻尼系数。

结果与 CppJieba 的 `KeywordExtractor` / `TextRankExtractor` 一致，分数逐位相同。两处差别：
CppJieba 对同分词的顺序未作规定；TextRank 中没有任何共现边的词（例如全文只剩一个候选词），
CppJieba 返回空的 `word`，本库返回该词本身，分数同为 0。
Python jieba 的 `extract_tags` / `textrank` 采用不同的规则（词频归一化、IDF 中位数、按词性过滤等），分数与本库不同。

| CppJieba | neo-cppjieba |
| --- | --- |
| `KeywordExtractor(dict, hmm, idf, stop_words, user_dict)` | `TfIdfExtractor{Jieba{dict, hmm, user_dict}, idf, stop_words}` |
| `TextRankExtractor(dict, hmm, stop_words, user_dict)` | `TextRankExtractor{Jieba{dict, hmm, user_dict}, stop_words}` |
| `Extract(s, words, topN)` | `extract(s, top_n)` |
| `Word::offsets` | `Keyword::positions[i].source.begin` |

## 从内存加载词典

`neo/DictSource.hpp` 的 `DictSource` 表示一份输入：`DictSource::file(path)` 在加载时读取文件，
`DictSource::memory(text, name)` 直接解析内存中的 UTF-8 文本，适合把词典编译进程序（如 `#embed`、
资源段）或在 WASM 等不便访问文件系统的环境中使用。格式与对应文件相同；`name` 只用于错误信息。
内存文本是借用的，只需在构造期间有效；构造完成后的对象不再引用它。

```cpp
#include "neo/Jieba.hpp"

using namespace neo_cppjieba;
extern const std::string_view embedded_dict, embedded_hmm, embedded_user_dict;

const auto user_dicts = std::array{DictSource::memory(embedded_user_dict, "user.dict")};
const auto jieba = Jieba{DictSource::memory(embedded_dict, "jieba.dict"), DictSource::memory(embedded_hmm),
                         user_dicts};
```

用户词典可以有多个，按顺序加载，同一个词以最后一次出现为准，对应 CppJieba 用 `;` 分隔的多个用户词典路径。
文件与内存来源可以混用。`TfIdfExtractor`、`TextRankExtractor` 同样接受 `DictSource`：

```cpp
const auto extractor = TfIdfExtractor{jieba, DictSource::memory(embedded_idf), DictSource::memory(embedded_stop_words)};
```

## 运行时增删词

`Jieba` 可在构建后就地加词、删词，对应 CppJieba 的 `InsertUserWord` 和 Python jieba 的 `add_word` / `del_word`：

```cpp
auto jieba = Jieba{dict_path, model_path, ""};
jieba.add_word("杭研", PosTag{"nz"});                // 默认用户词权重
jieba.add_word_with_freq("云计算", 500, PosTag{"n"}); // 频率必须为正
const bool removed = jieba.remove_word("大厦");        // 词不存在时返回 false
```

- 运行时加入的词与写在用户词典中的同一行效果相同：权重始终按加载时主词典的频率总和归一化，增删词不改变其他词的权重。
- 重复加入同一个词以最后一次为准。`remove_word` 对主词典和用户词典中的词一视同仁，删除后不会恢复更早的权重。
- 空词、非法编码、非正频率，以及超出 `TrieNodeId` 容量的加词通过 `LogConfig::Exception` 报告，词典保持不变。

增删词是非 `const` 操作，库内部不加锁。调用期间该 `Jieba` 不能同时执行其他调用；
调用方按场景自行选择互斥锁、读写锁或单线程使用。分词入口仍是 `const`，并发分词时各线程使用独立的 `Workspace`。
`TfIdfExtractor`、`TextRankExtractor` 引用构造时传入的 `Jieba`，会直接看到其后的增删。

## 位置与工作区

位置采用半开区间 `[begin, end)`：

- `position.source` 按原文编码单元计数，UTF-8 对应字节偏移，可用于原文 `substr`。
- `position.runes` 按 Unicode 码点计数，可用于编码转换的边界映射。
- FULL 和 SEARCH 可产生重叠区间。

编码转换后的分词结果对应转换后的文本；定位原始 GBK / GB18030 字节的方法见
[原始字节映射](encoding.md#original-byte-slices)。

连续处理文本时，把 `Workspace` 和输出数组放在循环外复用容量。
每个并发或嵌套调用独占一个工作区，输入和输出使用独立存储。
`workspace.release()` 可主动释放保留的缓冲。参见 [batch_cut.cpp](../examples/batch_cut.cpp)。

## 容量与错误处理

[Config.hpp](../include/neo/Config.hpp) 集中定义容量类型，默认均为 32 位：

| 配置 | 约束的范围 |
| --- | --- |
| `RuneIndex` | 单次输入的 Unicode 码点位置 |
| `SourceOffset` | 原文编码单元位置 |
| `DagOffset` | DAG 物化接口中的边偏移 |
| `TrieNodeId` | 主词典、用户词典和运行时加词合并后的 Trie 节点编号 |

可选 8、16、32、64 位及工具链支持的原生 128 位无符号整数。
容器大小和内存分配仍受宿主平台范围约束。调用方保证输入及词典规模符合所选容量；Debug 构建通过断言检查。
升级头文件或修改配置后，所有使用本库的编译单元采用同一配置重新编译。

文本入口在解码时校验 UTF，词典、模型和外部数据错误通过 `LogConfig::Exception` 报告。
发生异常后，丢弃该次调用可能留下的部分输出；后续调用可继续复用工作区。
内部不变量由断言检查。独立编码预检可使用 `neo/Unicode.hpp` 的 `is_valid_utf(input)`；
其用途和编码识别的区别见 [UTF validation](encoding.md#utf-validation)。
