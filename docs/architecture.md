# 架构

本文说明头文件的分层、依赖方向和扩展方式，供修改库内部时参考。使用方式见 [使用指南](usage.md)。

## 分层

依赖只从上往下，同层之间不互相包含：

| 层 | 头文件 | 职责 |
| --- | --- | --- |
| 门面 | `Jieba.hpp`、`KeywordExtractor.hpp` | 公开入口：解码、调用分派、按输出类型填充结果 |
| 结果与输入 | `Token.hpp`、`TokenView.hpp`、`TaggedToken.hpp`、`PosTag.hpp`、`Workspace.hpp`、`DictSource.hpp`、`CutMode.hpp` | 公开值类型，不含算法 |
| 分派 | `detail/Segmenter.hpp` | `CutMode` × `SegmentationStyle` → 具体算法，唯一的分支点 |
| 算法 | `detail/MPSegment.hpp` → `MixSegment.hpp` → `QuerySegment.hpp`；`FullSegment.hpp`、`HMMSegment.hpp`、`StyledSegment.hpp` | 在 rune 序列上产出 `WordRange`，不接触原文编码 |
| 算法公共件 | `detail/SegmentCommon.hpp`、`SegmentScratch.hpp`、`SegmentPolicy.hpp` | 分隔符切分、结果校验、可复用缓冲、字符分类 |
| 数据 | `detail/DictTrie.hpp`、`Trie.hpp`、`HMModel.hpp` | 构建后只读的词典和模型 |
| 基础 | `Config.hpp`、`Traits.hpp`、`Unicode*.hpp`、`detail/Logging.hpp`、`StringUtil.hpp`、`FileIO.hpp` | 容量配置、编码、诊断、解析 |

`Dag.hpp` 仅供 `Trie::find_dag` 的诊断和对照测试使用，分词路径不物化 DAG。

## 一次分词的数据流

```text
input ──as_view──▶ source ──decode_with_offset_into──▶ runes + offsets   (Workspace::decoded_)
runes ──detail::segment_into(mode)──▶ WordRange[]                         (Workspace::ranges_, scratch_)
WordRange[] + offsets ──fill_output / emit_tokens / fill_tagged_output──▶ 调用方的输出类型
```

- 算法只看 rune 坐标；原文坐标由门面在最后一步通过 offsets 换算，所以三种编码共用同一套算法实例。
- 词性标注在分词之后按结果查表，`Tagging::None` 的实例化不含任何标注代码。

## 两个维度的分派

- **运行时**：`CutMode` 由调用方逐次选择，在 `detail::segment_into` 中经一个 `switch` 落到 `cut_precise<hmm>`、
  `cut_search<hmm>`、`cut_full` 或 `HMMSegment`。MP 与 MIX_NO_HMM 是同一条路由。
- **编译期**：`compile_config::segmentation_style` 选择 CPP / RUST / PYTHON 规则。
  `cut_precise` 等函数各用一个 `if constexpr` 选择 CPP 算法或 `StyledSegment.hpp` 的参考实现；
  未选中的分支不实例化。少数无法上提的细节（Viterbi 的平局规则、权重归一化、HMM 前的切块）
  仍在各自算法内部以 `if constexpr` 区分。

门面和关键词提取不感知风格。新增一种风格时，只需在 `Segmenter.hpp` 的四个入口和上述细节处增加分支。

## CPP 风格的算法组合

```text
for_each_separated（按默认分隔符切段，分隔符单独成词）
  └─ 每段：mp_build_route_from_matches（直接遍历 Trie 匹配做反向 DP，不建 DAG）
       ├─ MP / MIX_NO_HMM：mp_emit_route
       ├─ MIX：mix_emit_route<true>，单字串交给 hmm_emit_one_segment
       └─ SEARCH：同一路由，建路由时记录 2/3 字词表成员，发词前补出子词
FULL：for_each_separated + 每个起点的全部匹配
HMM：for_each_separated + hmm_emit_one_segment
```

算法通过回调 `emit(WordRange)` 交付局部坐标的词，由外层加上段偏移写入结果，因此 MIX 与 SEARCH
共用同一个路由遍历，只是回调不同。

## 内存与并发

- `Jieba`、`DictTrie`、`HMModel` 构建后不可变，所有分词入口是 `const`。
- 每次调用的可变状态都在调用方的 `Workspace` 中：解码缓冲、结果缓冲和 `SegmentScratch`。
  `SegmentScratch` 的各缓冲保留最高水位，算法只覆盖本次读取的行，不做整体清零。
- `KeywordWorkspace` 内嵌一个 `Workspace`，关键词提取不与分词共享可变状态。

## 公开边界

公开 API 是 `neo/` 顶层头文件中、命名空间 `neo_cppjieba`（非 `detail`）里的类型和函数，
包括 `neo/PosTag.hpp` 的 `PosTag` 和 `pos::` 常量。`detail/` 目录中的内容都在 `neo_cppjieba::detail` 中，不保证兼容；
测试内部实现的用例通过 `using namespace neo_cppjieba::detail;` 显式声明这一点，只测公开接口的用例不引入它。

一处例外：`detail/Logging.hpp` 的 `check`、`log`、`assert_check` 仍在 `neo_cppjieba` 中，
因为错误通过公开的 `LogConfig::Exception` 报告。它们随 [Roadmap](../ROADMAP.md#日志与错误处理) 的日志改造一起调整。
