# Bimodal Content Defined Chunking for Backup Streams

复现论文 *Bimodal Content Defined Chunking for Backup Streams*（Kruus, Ungureanu, Dubnicki）的实验项目。
以 C++20 实现内容定义分块（CDC）与去重，目前包含：

- **baseline（论文 §2.2）**：基于 TTTD 的滑动窗口分块，配合全局内容去重。
- **拆分式（论文 §2.3，breaking-apart）**：大块扫描 + 精确存在性查询，只在重复/新数据的交界处改用小块。
- **§2.4 amalgamation**：尚未实现。

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

### 拆分式 2.3（breaking-apart）

1. 用大块器（baseline TTTD）扫一遍，对每个大块做**精确存在性查询**（`lookup`）。
2. 大块以前存过 → 原样按大块发射（去重）。
3. 没存过但前/后相邻大块存过 → 这是 **change region**，只对该区间用小块器现算切点并重切。
4. 其余大片新数据 → 仍按大块发射。

小块切点**按需**计算，不做整文件预扫描；查询约“每大块一次”。大块尺寸可在菜单里放大（1×/4×/16×/32×）。

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
│   ├── Breaking-Apart-Design.md          # 2.3 设计文档
│   ├── Breaking-Apart-Code-Guide.md      # 2.3 代码导读 + 文件阅读顺序
│   └── ChunkStore-Dedup-Walkthrough.md   # ChunkStore 查重逻辑详解
├── src/
│   ├── Hash.h / Hash.cpp                 # SHA-1 封装（内容摘要 / 窗口哈希 / 桶哈希）
│   ├── DataAndMethod.h / .cpp            # TTTD 参数、类型、存储全局量、baseline pFinder
│   ├── ChunkStore.cpp                    # 去重存储：chunkStore / isExist / lookup
│   ├── BreakingApart.h / .cpp            # 2.3 拆分式
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
    src/BreakingApart.cpp src/Hash.cpp \
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
  4) DataSet_3 —— 拆分式（论文 2.3）
  5) DataSet_4 —— 合成备份流（baseline TTTD）
  6) DataSet_4 —— 拆分式（论文 2.3）
```

选 `4`/`6` 后会再询问大块平均尺寸：`1≈1k / 2≈4k / 3≈16k / 4≈32k`（小块 = 大块 / 4）。

输出含义：

```text
totalChunks      发射的块总数（含重复）
uniqueChunks     真正存储的唯一块数
duplicateChunks  命中去重的块数
totalBytes       输入总字节
uniqueBytes      实际存储字节
dedupRatio       = totalBytes / uniqueBytes（即论文的 DER）
```

拆分式额外输出：`bigChunks / dupBigChunks / queries / rechunkRegions / smallChunks`。

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

## 5. 文档索引

| 文档 | 内容 |
| --- | --- |
| [`docs/Breaking-Apart-Design.md`](docs/Breaking-Apart-Design.md) | 2.3 的设计推导、参数、陷阱、验证方法 |
| [`docs/Breaking-Apart-Code-Guide.md`](docs/Breaking-Apart-Code-Guide.md) | 代码逐层讲解、文件/函数阅读顺序、Q&A |
| [`docs/ChunkStore-Dedup-Walkthrough.md`](docs/ChunkStore-Dedup-Walkthrough.md) | 去重存储的逐行解析与不变量 |

---

## 6. 已知结果与限制

- **DataSet_3（真实 emacs 未压缩 tar）**：baseline DER ≈ 1.93；拆分式在重复率高的相邻版本上单版本 DER 更高（如 +6%/+20%），但平均块长变小——因为 emacs 的变更偏分散，change region 过多。这与论文"emacs 提升最小"的说明一致。
- **DataSet_4（合成集中变更）**：大块取 ~16k 时，拆分式 DER ≈ 7.01，而同尺寸 baseline 约 6.0，方向与论文 Fig.6 一致。
- 窗口哈希当前使用 SHA-1 截断 64 位；与论文未公开的 Rabin fingerprint 不同，跨平台可复现但非逐位一致。
- 全部统计为单机端到端结果，仅适合项目内部横向比较。

---

## 7. AI 协作声明

本项目在开发、实验整理和文档撰写过程中使用了 AI 编程助手。以下声明用于说明当前仓库中可追溯的协作分工，**不表示 AI 独立完成了项目**。

### 用户负责或主导的内容

- 提出项目目标：复现论文 *Bimodal Content Defined Chunking for Backup Streams*，并搭建 TTTD baseline 与拆分式（2.3）。
- 确定算法方向、参数口径（`mainD/secondD/minT/maxT/window`、小块 = 大块 / k、k=4）与关键取舍（Fig.1 第 6 行按 `false` 修正、小块按需区间计算）。
- 准备与组织数据集，决定派生数据集方案（解压 tar、合成集中变更备份流）。
- 决定代码结构（合并头文件、返回类型后置写法）、分支策略、提交与推送，以及最终保留哪些实现。
- 审阅 AI 生成的代码、注释与文档，并在真实数据集上核查结果、判断是否重跑。

### AI 协助实现的内容

- 实现 `ChunkStore` 去重与索引（`chunkStore` / `isExist` / `lookup`）。
- 实现论文 2.3 拆分式（`src/BreakingApart.*`）：大块查询、相邻非重复的小块重切、内部新鲜区保持大块；以及按需区间小块切点。
- 迁移并整理 TTTD 分块核心，抽取 `ChunkerParams`，统一为返回类型后置写法。
- 搭建驱动层：数据集菜单、逐文件二进制读取、文件排序、逐文件与整体统计输出。
- 编写合成数据集脚本 `tools/make_synthetic_backups.py` 与 `docs/` 下的设计/导读/查重文档。
- 维护 CMake、补充注释、检查 `-Wall -Wextra` 零警告并执行上述数据集上的运行与统计。

### 共同验证方式

- AI 的改动均在本地用 C++20 编译器（`g++ -Wall -Wextra`，链接 OpenSSL）编译通过，并在用户指定数据集上运行后核对统计。
- 用户会根据实验目标要求更换数据集、调整参数、重新运行，并核查异常与结论。
- Git 提交与推送均在用户明确要求后执行；生成的大数据集与二进制产物保持在 `.gitignore` 范围内。

### 边界与限制

- 窗口哈希采用 SHA-1 截断，不等同于论文未公开精确参数的 Rabin fingerprint；与论文数值的比较只能作为参考。
- emacs 数据集的变更较为分散，拆分式的收益在该数据上不明显；合成数据集用于验证 P1/P2 成立时的趋势。
- 算法选择、实验结论与对外发布由用户负责；AI 仅作为开发与分析协作工具。

---

## 8. 参考

- Erik Kruus, Cristian Ungureanu, Cezary Dubnicki. *Bimodal Content Defined Chunking for Backup Streams*. FAST 2010.
- 论文 PDF 见 [`Paper/`](Paper/)。
