# Changelog

## Unreleased

## 1.1.0 - 2026-10-08

本版增加可选编码转换，并修复 CPP 规则下词典权重精度导致的分词兼容性问题。

### Added

- 新增可选 `neo/encoding/IcuCodec.hpp`，支持显式指定 UTF-8、GBK、GB18030 的双向转换，
  并提供 Unicode 码点边界与编码字节位置的映射。
  通过 `CPPJIEBA_ENABLE_ICU=ON` 启用，链接 `neo_cppjieba::icu`；默认核心库仍无 ICU 依赖。
- 新增 `neo/Unicode.hpp` 中的 `is_valid_utf(input)`，按输入字符类型检查 UTF 编码合法性，
  无需分配解码缓冲。该接口不检测输入的原始编码。
- 增加 [编码转换说明](docs/encoding.md)，覆盖借用结果生命周期、原始字节位置映射及 ICU 依赖配置。
- 增加手动触发的 CI benchmark 文档更新：在 GitHub 托管 runner 上测量，生成图表和数据快照，
  通过文档 PR 更新 README 的性能数据。

### Changed

- 将分词规则相关分支改为 `if constexpr`，在编译期选择 CPP、RUST、PYTHON 对应实现。
- CMake 项目声明版本 `1.1.0`，可通过 `modern_cppjieba_VERSION` 获取。

### Fixed

- 所有分词规则统一使用 `double` 词典权重和 MP 路径评分，恢复旧 CppJieba 的评分精度。
  CPP 规则此前使用 `float`，可能将不同概率路径舍入为相同分数，进而选择错误路径。
- 增加近似平局回归测试：词频为 `甲=9999`、`乙丙=10001`、`甲乙=10000`、`丙=10000` 时，
  `甲乙丙` 正确切分为 `甲乙 / 丙`；此前的 `float` 评分会选择 `甲 / 乙丙`。

### Upgrade notes

- 升级后重新编译所有使用本库头文件的编译单元；词典权重及相关内部结构的布局已经变化。
- CPP 规则下受浮点舍入影响的文本可能得到不同分词结果。词典及模型文件格式沿用 `1.0.0`。
- 恢复 `double` 精度会增加相关数据结构大小，部分查询的耗时可能增加。

## 1.0.0 - 2026-09-28

modern-cppjieba 的首个独立版本。

### Added

- 新增 `include/neo/` 下的 C++23 头文件实现，使用 `neo_cppjieba` 命名空间。
- 提供 MIX、MP、HMM、FULL、SEARCH 分词，以及显式关闭 HMM 的模式。
- 支持 UTF-8、UTF-16、UTF-32 输入，返回原文编码单元位置和 Unicode 码点位置。
- 增加借用 token、拥有原文、独立字符串、回调与预解码码点等输出入口。
- 提供调用方管理的 `Workspace`，用于连续分词时复用解码和算法缓冲。
- 增加编译期 `CPP`、`RUST`、`PYTHON` 分词规则配置，以及文本索引、偏移和 Trie 节点的容量类型配置。
- 增加现代实现单元测试、应用示例及 CppJieba / jieba-rs / Python jieba 比较工具。
- 增加 macOS、Windows 平台适配和 CI 构建配置。
- README 增加 CppJieba、modern-cppjieba、jieba-rs 和 Python jieba 的性能折线图，并保存测量快照与绘图脚本。

### Changed

- `Jieba` 构造要求显式提供主词典、HMM 模型和用户词典路径；空用户词典路径表示禁用用户词典。
- 分词入口改为显式 `CutMode` 和按输出形式命名的方法，替代早期现代实现中的 `CutMethod` 模板入口。
- Unicode 编解码通过 `neo/Unicode.hpp` 提供；词典、模型及分词算法等实现头移入 `neo/detail/`，
  公共入口不再暴露 `dict()`、`model()`。
- 通过 Trie 转移存储、直接计算 MP 路径、复用 HMM 缓冲，以及合并 MIX / SEARCH 匹配流程减少中间数据；
  FULL 直接输出匹配，分词热路径不再构造完整 DAG。
- `cut_runes_into` 发生异常时直接传播，输出可能保留部分结果；调用方应丢弃该次输出。
- 将原版 CppJieba 和其他比较依赖隔离在 `deps/`，现代库安装仅包含 `neo/` 头文件及词典数据。
  单测、示例和 benchmark 可分别启用，benchmark 不作为 CTest 测试。
- 精简 README，集中说明接入、结果所有权及配置约束；更新日志独立记录现代实现的演进。
- 合并示例目标的公共链接配置，补充 Visual Studio 构建与运行说明。

### Fixed

- 修复零权重词条、重复词典键和分词评分相关问题。
- 加强非法 Unicode、词典和 HMM 模型输入的校验，并补充分词范围与状态不变量检查。
- 修复 Windows 编译问题，并支持以 UTF-8 参数打开 Windows 路径。

## 上游历史

原 CHANGELOG 中的 v2.1.1–v5.5.0 属于 **CppJieba**，不作为 modern-cppjieba 的发布版本。
完整内容保留在 [改写前的历史文件](https://github.com/AkrinW/modern-cppjieba/blob/3732abc0e5548c96b4a6ea55113a02df97acf761/CHANGELOG.md)，
上游后续记录见 [CppJieba CHANGELOG](https://github.com/yanyiwu/cppjieba/blob/master/CHANGELOG.md)。
