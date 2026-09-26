这些示例分别演示一种应用场景，每个 `.cpp` 都可以独立阅读和修改。全部通过 `Jieba` 分词，
词典与 HMM 模型在程序启动时加载一次；示例使用 UTF-8 输入，第三个构造参数 `""` 表示不加载用户词典。
需要业务词典时，将它换成自己的词典文件路径。

从仓库根目录构建，需要支持 C++23（包括 `<print>`）的编译器：

```sh
cmake -S . -B build-examples \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DCPPJIEBA_BUILD_EXAMPLES=ON
cmake --build build-examples -j
```

示例通过 `neo_cppjieba::neo_cppjieba` 获取头文件路径和 C++23 编译要求；不下载测试或比较库依赖。
以下命令均在仓库根目录运行。第一个参数是词典目录，也可以改成安装后的 `share/cppjieba/dict/`。

| 示例 | 场景 | 主要接口 |
| --- | --- | --- |
| [basic_cut.cpp](basic_cut.cpp) | 对比七种分词模式 | `cut`、`CutMode` |
| [batch_cut.cpp](batch_cut.cpp) | 从文件或管道逐行分词，复用临时缓冲 | `cut_with_workspace`、`Workspace` |
| [search_highlight.cpp](search_highlight.cpp) | 搜索词定位和原文高亮 | `SEARCH`、`position.source`、`position.runes` |
| [word_count.cpp](word_count.cpp) | 流式统计 token 频次 | `cut_each`、`TokenView` |
| [owned_results.cpp](owned_results.cpp) | 保存请求处理函数返回的分词结果 | `cut_owned`、`cut_strings` |

先用模式对比示例观察同一段文本的结果：

```sh
./build-examples/examples/jieba_basic_cut dict '我来到北京清华大学'
```

日常文本可从 `MIX` 开始；`MIX_NO_HMM` 显式关闭其中的 HMM 未登录词识别。
`MP` 使用词典最大概率路径，`HMM` 使用模型分词；`FULL` 输出词典匹配，`SEARCH` 在混合分词基础上
补充便于检索的短词。`SEARCH_NO_HMM` 是关闭 HMM 的搜索模式。FULL 和 SEARCH 的结果可能相互重叠。

批量示例每行输出一行，用方括号分隔 token；空行仍输出空行：

```sh
printf '中国科学院\n我来到北京清华大学\n' | ./build-examples/examples/jieba_batch_cut dict
./build-examples/examples/jieba_batch_cut dict < test/testdata/testlines.utf8
```

`Workspace` 位于循环外，解码和算法缓冲跨行复用。`cut_with_workspace` 每次返回独立的位置数组，
其中的词视图仍借用当前输入行，因此示例在下一次 `getline` 改写原文前完成输出。
同时需要复用输出数组时，可改用 `cut_into`：

```cpp
neo_cppjieba::Workspace workspace;
std::vector<neo_cppjieba::TokenPosition> positions;
jieba.cut_into(text, neo_cppjieba::CutMode::MIX, positions, workspace);
```

工作区由调用方决定生命周期，也可以在用户代码中声明为 `thread_local`。
同一工作区同一时刻只服务一次调用；并发或 visitor 内嵌套分词使用不同工作区。
`workspace.release()` 可主动释放保留的缓冲。

搜索示例匹配一个完整的 SEARCH token，并分别展示每次命中的高亮：

```sh
./build-examples/examples/jieba_search_highlight dict '中国科学院' '科学'
```

输出包含 `bytes=[6, 12) runes=[2, 4)` 和 `中国[科学]院`。
UTF-8 字符串的 `substr` 使用 `position.source` 字节偏移；`position.runes` 是码点位置，
不能用来截取 UTF-8 字节串。这是 token 精确匹配示例，多词查询需要应用层自行处理。

词频示例从标准输入读取文本，按词的字符串顺序输出 `次数<TAB>词`：

```sh
printf '北京\n北京\n上海\n' | ./build-examples/examples/jieba_word_count dict
```

这里用 visitor 直接累加，不创建每行的公开 token 数组。map 的 key 使用 `std::string` 拥有词文本，
因此输入行被改写后仍然有效。示例统计所有分词结果，包括标点；停用词过滤可在 visitor 中添加。

需要保存函数返回的结果时，运行所有权示例：

```sh
./build-examples/examples/jieba_owned_results dict '我来到北京清华大学'
```

`tokenize_message` 把参数字符串移动给 `cut_owned`，返回值拥有整段原文及位置数组；函数返回后可继续读取。
`cut_strings` 则返回每个词独立拥有存储的字符串数组。示例分别展示两种输出方式，应用通常选择其中一种。
借用型 `cut` / `cut_with_workspace` 适合原文保持有效且稳定的场景；`cut_owned` 结果移动后应重新取得词视图。

所有示例在参数不正确时输出用法并返回 2，在词典、模型、输入编码或读取失败时输出错误并返回 1。
