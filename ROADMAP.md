# Roadmap

本文记录 modern-cppjieba 在 1.2.0 之后的计划方向。版本划分表示优先级，不承诺发布日期；
各项完成后移入 [CHANGELOG](CHANGELOG.md)。

## 现状

1.2.0 已提供：

- MIX、MIX_NO_HMM、FULL、SEARCH、SEARCH_NO_HMM、HMM、MP 分词，以及编译期 CPP / RUST / PYTHON 规则。
- UTF-8、UTF-16、UTF-32 输入，借用、持有原文和独立字符串三种结果，可复用 `Workspace`。
- 可选 ICU 编码转换、C ABI v1、可安装的 CMake 包。
- Linux x64/ARM64、macOS ARM64、Windows x64 CI，以及与 CppJieba、jieba-rs、Python jieba 的性能对比。

主要差距：原版 CppJieba 的词性标注、关键词提取和运行时加词尚未迁移；C ABI 只覆盖单次分词。

## 1.3 — 补齐 CppJieba 功能

从 CppJieba 迁移时最常见的阻碍，作为下一版本主线。

### 词性标注

[`detail/PosTag.hpp`](include/neo/detail/PosTag.hpp) 和词典中的词性字段已经存在，但 `Jieba` 未对外提供接口。

- [ ] 增加带词性的输出入口，例如 `cut_tagged`，以及 `cut_into` 可接受的带词性输出类型。
- [ ] 未登录词沿用上游规则：数字为 `m`，英文为 `eng`，其余为 `x`。
- [ ] 将根目录的 [词性标记表](词性标记.md) 移入 `docs/`，并在使用指南中引用。

### 关键词提取

[`dict/idf.utf8`](dict/idf.utf8) 和 [`dict/stop_words.utf8`](dict/stop_words.utf8) 随包安装，
目前只被 legacy 测试和 `benchmark/load_test.cpp` 使用。

- [ ] 新增独立的可选头文件 `neo/KeywordExtractor.hpp`，提供 TF-IDF 和 TextRank。
- [ ] 复用 `Workspace` 和借用结果，不给核心分词入口增加依赖。
- [ ] 与原版 `KeywordExtractor` / `TextRankExtractor` 的结果做比较测试。

### 词典加载与运行时加词

[`DictTrie`](include/neo/detail/DictTrie.hpp) 目前只能在构造时从文件加载。

- [ ] 支持从内存加载主词典、用户词典和 HMM 模型（`std::span<const char>` 或流），
  覆盖资源内嵌、WASM 等不便访问文件系统的场景。
- [ ] 支持追加用户词，对应 CppJieba 的 `InsertUserWord` 和 Python jieba 的 `add_word` / `suggest_freq`。
- [ ] 先确定并发模型。倾向保持 `Jieba` 构建后不可变：通过 builder 或派生新实例加入新词，
  调用方自行替换实例，避免在分词热路径上加锁。

## 1.4 — 扩展 C ABI v1

只新增符号，不改变已有 v1 结构布局和函数语义。

- [ ] **明确并发契约。** 头文件要求同一 engine 的调用串行，而 C++ 的 `Jieba::cut*` 是 `const`，
  且每次调用使用独立 `Workspace`。确认 `Logging` 等静态状态无数据竞争后，将契约放宽为同一 engine
  可并发分词，并增加 TSan 测试。这决定语言绑定能否在分词时释放 GIL 或跨线程共享 engine。
- [ ] 增加可复用的 workspace 和结果对象，例如 `neo_jieba_v1_workspace_create` 与 `cut_utf8_into`，
  避免每次调用重新分配和复制原文；提供只返回字节区间、不复制原文的变体。
- [ ] 从内存创建 engine（依赖 1.3 的内存加载）。
- [ ] 1.3 的 C++ 接口稳定后，暴露词性和关键词提取。
- [ ] 日志回调，与下文的日志策略一致。

## 1.5 — 库的健壮性

### CI 质量门禁

当前 CI 未运行 sanitizer、fuzz、覆盖率，仓库中的 [`.clang-tidy`](.clang-tidy) 和
[`tools/`](tools) 脚本也未接入 CI。

- [ ] ASan + UBSan 任务，覆盖单元测试和 C ABI 测试。
- [ ] TSan 任务，配合 C ABI 并发契约。
- [ ] libFuzzer 目标：UTF 解码、主词典 / 用户词典 / HMM 模型解析、`neo_jieba_v1_cut_utf8_owned`。
- [ ] clang-format 与 clang-tidy 检查。
- [ ] llvm-cov 覆盖率报告。

### 日志与错误处理

[`detail/Logging.hpp`](include/neo/detail/Logging.hpp) 直接写 stderr，断言失败时调用 `std::terminate`。

- [ ] 提供可替换的日志输出，并允许完全静默。
- [ ] 区分调用方输入错误（异常或状态码）与内部不变量破坏（保持 terminate），在文档中列出各入口的失败方式。
- [ ] 评估 `std::expected` 风格的入口，例如 `Jieba::open(...)` 和 `try_cut`，服务禁用异常的调用方。

### 加载性能

- [ ] 评估预编译的二进制词典 / Trie 快照，支持 mmap 加载，缩短服务冷启动和多进程部署的初始化时间。
  以 [`dict_trie_benchmark`](benchmark/dict_trie_benchmark.cpp) 和 `load_test` 为基线。

### 降低工具链要求

当前要求 GCC 15、LLVM 23 / libc++ 或 Visual Studio 2026，主要因为使用了 `<print>` 和 `<format>`。

- [ ] 统计库头文件中对这些设施的依赖；若仅限日志和诊断，引入一层薄封装。
- [ ] 目标支持 GCC 14、Clang 18 和主流发行版自带工具链，并增加最低版本 CI 任务固定这条下限。

## 1.6+ — 生态与易用性

- [ ] **Python 绑定**：基于 nanobind 或 C ABI，通过 cibuildwheel 发布 wheel。
- [ ] 视需求提供 Rust `-sys` crate、Go cgo 绑定。
- [ ] **包管理器**：vcpkg port、Conan recipe、xmake 包。
- [ ] **发布产物**：tag 触发的 release workflow，将 C API CI 已生成的各平台包附加到 GitHub Release，
  附带校验和与构建来源证明。
- [ ] **英文文档**：`README.en.md` 和英文使用指南；生成 API 参考站点（Doxygen 或 mkdocs）。
- [ ] **集成示例**：WASM（emscripten）构建示例；SQLite FTS5 或搜索引擎分词器插件示例。

## 维护清理

不依赖版本节奏，可随时处理。

- [ ] [`include/neo/Jieba.hpp`](include/neo/Jieba.hpp) 中 `Jieba` 的类注释仍描述旧接口
  （"These usage notes describe the former facade"）；`cut_runes_into` 中的 "Former policy" 注释同理，改写为当前契约。
- [ ] 统一命名：项目名 modern-cppjieba、命名空间与包名 `neo_cppjieba`，词典却安装到 `share/cppjieba/dict`，
  可能与上游 CppJieba 的安装冲突。改为 `share/neo_cppjieba/dict` 并保留过渡期，同时在 CMake 包中导出词典目录变量。
- [ ] 删除 [`.github/workflows/cmake.yml`](.github/workflows/cmake.yml) 中的模板注释。
- [ ] 清点 `test/testdata/` 中上游 server 时代的文件（`server.conf`、`load_test.urls`、`curl.res`），移除不再使用的部分。
- [ ] 确认现代库不依赖 `deps/limonp`，在 [依赖说明](deps/README.md) 中注明它仅服务 legacy 测试。

## 优先顺序

1. C ABI 并发契约与 sanitizer / fuzz：成本低，直接提升可信度。
2. 1.3 的三项功能：决定 CppJieba 用户能否迁移。
3. 降低工具链要求与 Python 绑定：扩大可用人群最有效的两项。

欢迎通过 issue 讨论优先级或认领条目。
