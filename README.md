# Bimodal Content Defined Chunking for Backup Streams

复现论文 *Bimodal Content Defined Chunking for Backup Streams*（Kruus, Ungureanu, Dubnicki）的实验项目。
以 C++20 实现内容定义分块（CDC）与去重，**本分支（`合成式`）包含**：

- **baseline（论文 §2.2）**：基于 TTTD 的滑动窗口分块，配合全局内容去重。
- **合成式（论文 §2.4，chunk amalgamation）**：先用小块器切成小块，再把连续 k 个小块合成大块做存在性查询与去重。

> §2.3 拆分式（breaking-apart）在 `拆分式` 分支实现，本分支不包含其算法代码，只复用了它抽出的公共基础设施。
> 详细设计与代码导读见 [`docs/`](docs/)。

---

## 1. 算法概览

### baseline TTTD

在最小块长之后，优先采用主除数切点；主规则长时间不命中时用备份除数兜底；达到最大块长则强制切分。

| 参数 | 值 | 含义 |
| --- | ---: | --- |
| `mainD` | 540 | 主除数 |
| `secondD` | 270 | 备份除数 |
| `minT` | 460 | 最小块长（字节） |
| `maxT` | 2800 | 最大块长（字节） |
| `window` | 48 | 滑动窗口字节数 |

平均块长约 `minT + mainD ≈ 1000` 字节。内容哈希为完整 SHA-1（160 位）。

### 合成式 2.4（chunk amalgamation，k-fixed）

与拆分式相反，先细后粗（对照论文 Fig.3）：

1. 先用**小块器**把整条流切成 `m` 个小块；
2. 在窗口 `buf[0..2k-1]` 上做**前向搜索** `pos = 0..k`，找第一个重复的大块（恰好 `k` 个连续小块）：
   - 命中 → 先把 `pos` 个前导小块按小块发射，再把该大块按大块发射（`prevBigWasDup = true`）；
   - 未命中但刚离开重复区 → 发射 `k` 个小块作为 transition（`prevBigWasDup = false`）；
   - 都不是（大片新数据内部）→ 把前 `k` 个小块**合成一个大块**发射；
3. 剩余不足 `k` 个小块时，全部按小块发射（尾块）。

大块在源数据里就是一段连续字节（`k` 个相邻小块首尾相接），因此查询/发射都无需真正拼接内容。
查询量：每大块最多 `k` 次（每小块一次），比拆分式的「每大块一次」更高。大块平均尺寸可在菜单里放大（1×/4×/16×/32×）。

---

## 2. 项目结构

```text
.
├── CMakeLists.txt
├── README.md
├── TODO.md
├── Paper/                      # 论文 PDF
├── Dataset/                    # 数据集（大文件已 gitignore，见 §4）
│   ├── DataSet_1/              # 原始 .tar.gz（未入库）
│   ├── DataSet_2/              # 解压源码树（未入库）
│   ├── DataSet_3/              # 由 DataSet_1 解压得到的未压缩 tar（未入库）
│   └── DataSet_4/              # 脚本合成的备份流（未入库）
├── docs/
│   ├── Amalgamation-Design.md            # 2.4 设计文档
│   ├── Amalgamation-Code-Guide.md        # 2.4 代码导读 + 文件阅读顺序
│   ├── Breaking-Apart-Design.md          # 2.3 背景设计（算法实现见 拆分式 分支）
│   └── ChunkStore-Dedup-Walkthrough.md   # ChunkStore 查重逻辑详解
├── src/
│   ├── Hash.h / Hash.cpp                 # SHA-1 封装（内容摘要 / 窗口哈希 / 桶哈希）
│   ├── DataAndMethod.h / .cpp            # TTTD 参数、类型、存储全局量、baseline pFinder
│   ├── ChunkStore.cpp                    # 去重存储：chunkStore / isExist / lookup
│   ├── Chunker.h / Chunker.cpp           # 公共纯 TTTD 分块器（baseline 与 2.4 共用）
│   ├── Amalgamation.h / .cpp             # 2.4 合成式
│   └── Baseline.cpp                      # 驱动：菜单、读文件、逐文件统计
└── tools/
    └── make_synthetic_backups.py         # 生成"集中反复变更"的合成备份流
```

---

## 3. 构建

依赖：支持 C++20 的编译器、CMake、OpenSSL（Crypto）。

### CMake

```bash
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug
```

### 直接编译

```bash
g++ -std=c++20 -O2 -Wall -Wextra \
    src/Baseline.cpp src/DataAndMethod.cpp src/ChunkStore.cpp \
    src/Chunker.cpp src/Amalgamation.cpp src/Hash.cpp \
    -o baseline -lcrypto
```

---

## 4. 运行

启动后选择运行项：

```text
请选择运行项：
  1) DataSet_1 —— tar.gz 压缩包（baseline TTTD）
  2) DataSet_2 —— 解压源码树（baseline TTTD，递归）
  3) DataSet_3 —— 未压缩 tar（baseline TTTD）
  4) DataSet_3 —— 合成式（论文 2.4）
  5) DataSet_4 —— 合成备份流（baseline TTTD）
  6) DataSet_4 —— 合成式（论文 2.4）
```

选 `4`/`6` 后会再询问大块平均尺寸：`1≈1k / 2≈4k / 3≈16k / 4≈32k`。
小块器固定为基准参数除以 4（平均约 250 B），大块 = `4×scale` 个小块，故大块平均约 `scale×1k`。

输出含义：

```text
totalChunks      发射的块总数（含重复）
uniqueChunks     真正存储的唯一块数
duplicateChunks  命中去重的块数
totalBytes       输入总字节
uniqueBytes      实际存储字节
dedupRatio       = totalBytes / uniqueBytes（即论文的 DER）
```

合成式额外输出：`smallChunks / bigChunks / dupBigChunks / queries / emittedSmalls`。

### 数据集准备

- **DataSet_3**（未压缩 tar）：把 `Dataset/DataSet_1/*.tar.gz` 解压成 `.tar` 后放入 `Dataset/DataSet_3/`。
  例如（gzip）：`gzip -d -k Dataset/DataSet_1/*.tar.gz`，再把生成的 `.tar` 移入 `DataSet_3/`。
- **DataSet_4**（合成备份流）：

  ```bash
  python tools/make_synthetic_backups.py
  ```

  默认以 `Dataset/DataSet_3/emacs-22.1.tar` 的前 32 MiB 为基线，生成 8 个备份；
  在 64 个固定 ROI 内反复局部替换，制造"大段重复 + 少量集中变更"的备份流（固定随机种子，可复现）。

`Dataset/DataSet_1~4/` 均已在 `.gitignore` 中忽略（体积过大）。

---

## 5. 已知结果与限制

在 **DataSet_4（8 × 32 MiB 合成备份流）** 上顺序处理，单机端到端结果：

| 配置 | `dedupRatio` (DER) | `totalChunks` | 平均块长 |
| --- | ---: | ---: | ---: |
| baseline（大块 ≈ 1k） | 7.774 | 268566 | ≈ 1000 B |
| 2.4 合成式（大块 ≈ 1k，k=4） | 7.871 | 277958 | ≈ 966 B |
| 2.4 合成式（大块 ≈ 16k，k=64） | 7.494 | 49748 | ≈ 5395 B |

- 在 DER 相近（≈7.5–7.9）的区间内，2.4 把平均块长从 baseline 的 ~1k 提高到 ~5.4k，方向与论文"相近 DER 下更大平均块长"一致。
- 该数据集有 64 个固定 ROI，局部变更会产生较多 transition；`k` 越大，每次"离开重复区"要发射 `k` 个小块，transition 越长，因此 DER 略降、块数骤减，这是预期行为。
- 窗口哈希当前使用 SHA-1 截断 64 位；与论文未公开的 Rabin fingerprint 不同，跨平台可复现但非逐位一致。
- 全部统计为单机端到端结果，仅适合项目内部横向比较。

---

## 6. 文档索引

| 文档 | 内容 |
| --- | --- |
| [`docs/Amalgamation-Design.md`](docs/Amalgamation-Design.md) | 2.4 的设计推导、参数、陷阱、验证方法 |
| [`docs/Amalgamation-Code-Guide.md`](docs/Amalgamation-Code-Guide.md) | 代码逐层讲解、文件/函数阅读顺序、Q&A |
| [`docs/Breaking-Apart-Design.md`](docs/Breaking-Apart-Design.md) | 2.3 背景设计（实现见 `拆分式` 分支） |
| [`docs/ChunkStore-Dedup-Walkthrough.md`](docs/ChunkStore-Dedup-Walkthrough.md) | 去重存储的逐行解析与不变量 |

---

## 7. AI 协作声明

本项目在开发、实验整理和文档撰写过程中使用了 AI 编程助手。以下声明用于说明当前仓库中可追溯的协作分工，**不表示 AI 独立完成了项目**。

### 用户负责或主导的内容

- 提出项目目标：复现论文 *Bimodal Content Defined Chunking for Backup Streams*，并在本分支实现合成式（2.4）。
- 确定算法方向、参数口径（小块 = 基准 / 4、大块 = k 个小块、k = 4×scale）与关键取舍（Fig.3 第 10 行按 `false` 修正）。
- 决定分支策略（`main` / `拆分式` / `合成式` 三支并存），并要求公共基础设施直接从 `拆分式` 摘出。
- 准备与组织数据集，决定派生数据集方案（解压 tar、合成集中变更备份流）。
- 决定代码结构（返回类型后置写法、独立 `Chunker` 模块），以及最终保留哪些实现。
- 审阅 AI 生成的代码、注释与文档，并在真实数据集上核查结果、判断是否重跑。

### AI 协助实现的内容

- 从 `拆分式` 分支摘出公共基础设施：`ChunkStore::lookup`、`DataAndMethod` / `Hash` 的后置返回类型、数据集菜单与工具脚本。
- 抽取 `src/Chunker.*`（`ChunkerParams` / `findBoundaries` / `findBoundariesInRange` / `deriveSmallParams`）。
- 实现论文 2.4 合成式（`src/Amalgamation.*`）：小块预切、前向搜索、前导小块 / transition / 合成大块的决策，以及尾部残余处理。
- 搭建驱动层：数据集菜单、模式选择、逐文件二进制读取、文件排序、逐文件与整体统计输出。
- 编写 `docs/` 下的设计与代码导读文档，维护 CMake，补充注释，检查 `-Wall -Wextra` 零警告并执行上述数据集上的运行与统计。

### 共同验证方式

- AI 的改动均在本地用 C++20 编译器（`g++ -Wall -Wextra`，链接 OpenSSL）编译通过，并在用户指定数据集上运行后核对统计。
- 额外用不变量测试校验：发射区间不重不漏、总 emit 字节等于输入、`uniqueChunks + duplicateChunks == totalChunks`。
- 用户会根据实验目标要求更换数据集、调整参数、重新运行，并核查异常与结论。
- Git 提交与推送均在用户明确要求后执行；生成的大数据集与二进制产物保持在 `.gitignore` 范围内。

### 边界与限制

- 窗口哈希采用 SHA-1 截断，不等同于论文未公开精确参数的 Rabin fingerprint；与论文数值的比较只能作为参考。
- 本分支不含 2.3 实现；两算法的横向对比需在合并 / 分别运行后进行。
- 算法选择、实验结论与对外发布由用户负责；AI 仅作为开发与分析协作工具。

---

## 8. 参考

- Erik Kruus, Cristian Ungureanu, Cezary Dubnicki. *Bimodal Content Defined Chunking for Backup Streams*. FAST 2010.
- 论文 PDF 见 [`Paper/`](Paper/)。
