# 设计原则

本文是项目对代码"放在哪里、写成什么形态"的统一约定。[架构](architecture.md) 描述现有的分层和数据流，
本文给出新增或重构代码时的判断标准。两者冲突时，以本文为准，并把架构文档更新为一致。

## 六种角色

每段代码先判断它属于哪种角色。角色之间解耦：算法不持有资源，资源不包含跨资源的算法，
配置和上下文从参数流入，不藏在全局或静态状态里。

| 角色 | 是什么 | 形态 | 生命周期与并发 | 现有例子 |
| --- | --- | --- | --- | --- |
| 资源 | 昂贵、构建后只在显式编辑时改变的数据 | 带不变量的类，只能移动，复制用 `clone()` | 调用方持有；`const` 访问可并发，编辑需独占 | `DictTrie`、`Trie`、`HMModel`、IDF 表、停用词 |
| 算法 | 组合资源、配置和上下文，产出结果 | policy（只有 static 成员的类）或 `detail` 自由函数 | 无状态 | `MPSegment`、`mp_cut_append`、`segment_into` |
| 配置 | 选择算法行为的廉价值 | `enum class` 或聚合 `struct`；编译期配置放在 `Config.hpp` | 按值传递，随调用流入 | `CutMode`、`TfIdfOptions`、`TextRankOptions`、`SegmentationStyle` |
| 上下文 | 一次调用的可变缓冲 | 调用方持有的对象，保留容量供复用 | 同一时间只属于一个调用 | `Workspace`、`SegmentScratch`、`KeywordWorkspace` |
| 契约 | 角色之间接口的编译期约束 | C++20 `concept` | 无运行时存在 | `StringLike`、`CharType`、`CapacityInteger`、`OutputValue`、`ViewVisitor` |
| 门面 | 公开入口 | 持有必须一起出现的资源，成员函数校验、拆包并转发 | 查询为 `const`，编辑为非 `const` | `Jieba`、`TfIdfExtractor`、`TextRankExtractor` |

依赖方向：门面 → 算法 → 资源。配置和上下文作为参数穿过这条链，契约约束每一处模板参数。

算法的统一签名顺序是 `(const 资源 &..., 配置, 输入, 输出 &, 上下文 &)`，与现有的
`segment_into(dict, model, runes, mode, out, scratch)` 和 `cut_into(input, mode, out, workspace)` 一致。

## 资源

- 资源是带不变量的类。构造时完成全部校验，非法的词典、模型或数据文件通过 `check` 报告为用户错误。
- 成员函数只处理该类型自己的数据：查询（`find`、`find_dag`）、自身数据结构的编辑（`insert`、`erase`、`compact`）。
  需要组合多个资源的操作不放在资源类里。
- 只能移动，不能隐式复制；需要复制时提供 `clone() const`，让整份数据的复制在调用点可见。
- 编辑是非 `const` 成员函数，库内不加锁。编辑入口在最外层用 `check` 校验公开参数，
  内部的数据结构操作只用 `assert_check` 维护不变量。
- 只在调用方确实需要单独持有或替换某个资源时，才把它提升为公开类型；否则留在 `detail` 中，保留重构空间。

## 配置

- 配置是廉价的值类型：`enum class` 或可用指定初始化器构造的聚合 `struct`，例如 `TextRankOptions{.window = 1}`。
- 不用默认参数表达"使用默认配置"，调用方显式传入 `Options{}`。
- 配置的合法性由接收它的门面或资源在入口处 `check`，例如 `TextRankOptions::damping`。
- 运行时配置随调用传入；影响实例化的配置（容量类型、`SegmentationStyle`）是编译期配置，放在 `Config.hpp`。

## 上下文

- 上下文由调用方持有，保留缓冲容量以供复用；结果不借用上下文的缓冲，只借用调用方的原文。
- 同一时间只属于一个调用。并发或嵌套调用各用一个上下文。
- 公开的上下文类型保持不透明。内部状态放在 `detail` 中的普通 `struct` 里，由门面拆包后传给算法。
- 算法只接收它实际读写的缓冲或 `detail` 状态结构，不接收整个公开上下文对象。

## 算法：policy 与自由函数

一组算法满足下列至少一条时写成 policy，否则写成 `namespace detail` 中的自由函数：

1. 会作为模板参数，在编译期于多种实现间选择。
2. 是一族可互换的实现，提供同一组签名，可以用一个 concept 约束。
3. 需要整组参数化或共享关联类型和常量，例如 `Policy<Style>::cut_into`、`Policy::Scratch`。

只有一个实现、直接按名字调用、也没有关联类型的类，应写成自由函数，不为它单独包一层类。

policy 的规则：

- 无状态：不含数据成员和静态可变状态，所有输入来自参数。
- 每一族 policy 有对应的 concept，每个实现旁边用 `static_assert(Concept<Impl>)` 检查是否满足。
- policy 负责选择和组合。被多个 policy 共用的积木（路由构建、Viterbi、DAG 遍历、TextRank 迭代）写成自由函数，
  policy 之间不靠继承或 friend 复用代码。
- 共用一个方法名约定：写入调用方输出的入口叫 `cut_into` / `extract_into`，返回新容器的便捷入口叫 `cut` / `extract`。

## 契约（concept）

concept 是角色之间接口的唯一书面定义，下列情况必须使用：

- 模板接受外部类型：输入（`StringLike`、`CharType`）、输出（`OutputValue`、`TaggedOutputValue`）、回调（`ViewVisitor`）。
- 一族 policy 的共同签名（见上一节）。
- 编译期配置的取值范围（`CapacityInteger`、`LogConfiguration`）。

规则：

- 用 concept 和 `requires` 子句替代 SFINAE、`enable_if` 和无约束的模板参数，让错误在调用点给出可读的诊断。
- concept 只描述语法要求；语义要求（复杂度、是否可重入、坐标系）写在 concept 上方 1–3 行注释中。
- 公开模板使用的 concept 放在公开头文件（如 `Traits.hpp`），只供内部使用的放在 `neo_cppjieba::detail`。
- 一个 concept 只对应一个角色的一个接口。只有一个满足者时直接写具体类型，不为它定义 concept。
- 公开 concept 的变更等同于公开 API 变更。

## 门面

- 门面持有"总是一起使用、彼此有约束"的资源。例如 `Jieba` 持有词典和 HMM 模型，用户词权重依赖主词典的频率总和。
- 成员函数只做四件事：用 `check` 校验公开参数、解码输入、调用算法、按输出类型填充结果。算法本身不写在门面里。
- 彼此独立的资源由各自的门面持有，其他门面在调用时以 `const &` 传入，不长期保存引用。
  例如提取器持有 IDF 和停用词，`Jieba` 由每次 `extract` 传入。
- 查询入口为 `const`；编辑入口为非 `const`，要求调用方独占访问。

## 访问控制与 friend

friend 只在两种情况下使用：

1. 门面与它专属的公开值类型之间，例如 `Workspace`、`Tokens` 对 `Jieba` 开放构造或内部缓冲。
2. 一个公开类型对唯一的 `detail` 访问函数开放，例如 `DictSource` 对 `detail::with_source_text`。

一个类型不为多个算法类分别开 friend；需要访问上下文内部缓冲的多处算法，经由唯一访问点取得 `detail` 状态结构，
再按需传参。

## 速查

```text
只读写某个类型自己的数据?                          → 该类型的成员函数（资源）
组合多个资源、配置、上下文?
├─ 作为模板参数 / 同签名的一族 / 需整组参数化?      → policy：无状态 + concept + static_assert
└─ 否                                              → namespace detail 自由函数
选择行为的廉价值?                                  → 配置：enum class 或聚合 struct，显式传入
每次调用的可变缓冲?                                → 上下文：调用方持有，内部状态放 detail struct
模板接受的外部类型或一族实现的共同签名?            → 契约：concept
公开入口?                                          → 门面：校验、解码、转发、填充
需要私有访问?                                      → 门面专属值类型，或唯一的 detail 访问函数
```

## 现状与待调整处

下列代码早于本约定，按约定逐个小 patch 调整，每次调整前先出 patch plan：

- `MPSegment`、`MixSegment`、`QuerySegment`、`FullSegment`、`HMMSegment` 是同签名的一族，
  但目前在 `detail/Segmenter.hpp` 中按名字直接调用，没有对应的 concept 和 `static_assert`。
- `KeywordWorkspace` 对 `TfIdfExtractor`、`TextRankExtractor` 分别开 friend，`collect` / `select` 是它的成员函数；
  应收敛为一个 `detail` 访问点，并把两者改为作用于 `detail` 状态结构的自由函数。
- `TextRankExtractor::rank` 是写在门面里的算法，应移到 `detail` 中的自由函数。
