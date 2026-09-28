# Changelog

记录 modern-cppjieba 中面向使用者的重要改动，按
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) 的方式分类。

本项目尚未发布独立版本，已有的现代实现改动统一收录在 `Unreleased`。
首次发布时，将对应条目归入实际版本号并补充发布日期；后续改动继续记录在 `Unreleased`。

## Unreleased

### Added

- 新增 `include/neo/` 下的 C++23 头文件实现，使用 `neo_cppjieba` 命名空间。
- 提供 MIX、MP、HMM、FULL、SEARCH 分词，以及显式关闭 HMM 的模式。
- 支持 UTF-8、UTF-16、UTF-32 输入，返回原文编码单元位置和 Unicode 码点位置。
- 增加借用 token、拥有原文、独立字符串、回调与预解码码点等输出入口。
- 提供调用方管理的 `Workspace`，用于连续分词时复用解码和算法缓冲。
- 增加编译期 `CPP`、`RUST`、`PYTHON` 分词规则配置，以及文本索引、偏移和 Trie 节点的容量类型配置。
- 增加现代实现单元测试、应用示例及 CppJieba / jieba-rs / Python jieba 比较工具。
- 增加 macOS、Windows 平台适配和 CI 构建配置。

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
