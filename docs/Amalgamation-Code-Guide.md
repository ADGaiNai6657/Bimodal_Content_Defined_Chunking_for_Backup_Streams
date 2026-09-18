# 合成式（2.4）代码导读

> 目的：把 `src/Amalgamation.*`、`src/Chunker.*` 及相关改动的代码讲清楚，读代码时不必再猜。
> 配套：算法设计见 `docs/Amalgamation-Design.md`；查重细节见 `docs/ChunkStore-Dedup-Walkthrough.md`。

---

## 0. 一句话理解整个流程

```
读一个文件(一次“备份”)的原始字节
   │
   ├─ 用【小块器】把它整条流切成许多“小块”        ← TTTD 分块（只跑这一遍）
   │
   └─ 按 k 个小块一组，向前滑动找“重复大块”
        找到重复大块  → 先把前面的小块零散发掉，再整块发这个大块
        刚离开重复区  → 连发 k 个小块（transition）
        大片新数据    → 把 k 个小块合成一个大块发出去
```

和 2.3 正好相反：2.3 是「先大块、再按需细切」，2.4 是「先小块、再合成大块」。

---

## 1. 阅读顺序

### 1.1 文件顺序（先定义、后实现、再调用）

| # | 文件 | 为什么要按这个次序读 |
| --- | --- | --- |
| 1 | `docs/Amalgamation-Design.md` | 先建立算法背景与设计取舍。 |
| 2 | `docs/Amalgamation-Code-Guide.md` | 本导读，给出全局地图。 |
| 3 | `src/Hash.h` | 最底层类型 `Sha1Digest` 与哈希接口。 |
| 4 | `src/DataAndMethod.h` | 全局类型、参数常量、存储声明、计数器。 |
| 5 | `src/Hash.cpp` | 哈希如何实现（内容摘要 / 窗口哈希 / 桶哈希）。 |
| 6 | `src/DataAndMethod.cpp` | 切片 `getSubString`、发射 `emitChunk`、baseline `pFinder`。 |
| 7 | `src/ChunkStore.cpp` | 去重与查询：`chunkStore` / `isExist` / `lookup`。 |
| 8 | `src/Chunker.h` / `src/Chunker.cpp` | 大/小块共用的纯分块器。 |
| 9 | `src/Amalgamation.h` | 2.4 的参数、接口与统计量。 |
| 10 | `src/Amalgamation.cpp` | 2.4 核心：小块预切 → 前向搜索 → 合成/transition。 |
| 11 | `src/Baseline.cpp` | 驱动层：菜单、读文件、逐文件统计。 |
| 12 | `tools/make_synthetic_backups.py` | 合成备份流数据集。 |
| 13 | `CMakeLists.txt` | 构建目标与依赖。 |

> 一句话原则：**类型声明（`*.h`）先于实现（`*.cpp`），底层原语先于上层算法，算法先于驱动与脚本。**

### 1.2 函数顺序（按数据流）

1. `src/Chunker.h` —— 先认类型：`ChunkerParams`(`:19`)。
2. `src/Chunker.cpp:14 findBoundariesInRange` / `:68 findBoundaries` —— 最基础的“怎么切”。
3. `src/Amalgamation.h:32 AmalgamationConfig` —— 2.4 的参数：`{ small, k }`。
4. `src/Amalgamation.cpp:60 processFileAmalgamation` —— 2.4 的主循环（最核心）。
5. `src/Amalgamation.cpp:33 emitSmallsAt` / `:48 emitBigAt` —— 主循环调用的两种发射。
6. `src/Amalgamation.cpp:93 queryBig` —— 大块“以前存过吗”（只读 `lookup`）。
7. `src/ChunkStore.cpp:71 lookup` —— 精确只读查询。
8. `src/DataAndMethod.cpp:51 emitChunk` —— 主循环与存储之间的桥。
9. `src/Baseline.cpp:117 processFile` / `:136 processDirectory` / `:185 resolver` —— 驱动层。
10. `tools/make_synthetic_backups.py` —— 合成测试数据。

---

## 2. 先认类型

### `ChunkerParams`（`Chunker.h:19`）

一套 TTTD 分块参数，大块器与小块器共用同一套切分代码：

| 字段 | 含义 | 影响 |
| --- | --- | --- |
| `mainD` | 主除数 | `hash % mainD == mainD-1` 命中就切；越大块越少 |
| `secondD` | 备份除数 | 主规则长时间不命中时的兜底候选 |
| `minT` | 最小块长 | 小于它不切 |
| `maxT` | 最大块长 | 到这个长度强制切 |
| `window` | 滑动窗口字节数 | 取哈希的窗口大小，本项目 48 |

> 直觉：平均块长 ≈ `minT + mainD`。小块器取基准的 1/4，平均约 250 B。

### `AmalgamationConfig`（`Amalgamation.h:32`）

```cpp
struct AmalgamationConfig {
    ChunkerParams small; // 小块器参数
    std::size_t k;       // 每个大块由 k 个连续小块合成（k-fixed）
};
```

2.4 只需要两件事：**用多细的小块器**、**几个小块合成一个大块**。大块尺寸由 `small` 的平均块长 × `k` 决定。

### 统计量（`Amalgamation.h:44-48`）

全局计数器，用来解释算法行为：

- `gAmSmallChunks`：小块器切出的小块总数。
- `gAmBigChunks`：合成并发射的大块数（含重复）。
- `gAmDupBigChunks`：其中命中重复的大块数。
- `gAmQueryCount`：大块存在性查询次数（论文：每大块最多 k 次）。
- `gAmSmallEmitted`：以小块的粒度单独发射的小块数（前导 + transition + 尾块）。

> 复用统计量前，驱动层会先调 `resetAmalgamationStats()`（`Amalgamation.cpp:157`）清零。

---

## 3. `findBoundaries`：纯分块，不存不发射

**职责**：给一段字节，返回切点位置（升序），**不碰全局存储**。

- `findBoundariesInRange(data, b0, b1, params)`（`Chunker.cpp:14`）：只扫 `[b0, b1]`，窗口取自整段 `data`（可越过 `b0` 左端），`min/max` 从 `b0` 起算。
- `findBoundaries(data, params)`（`Chunker.cpp:68`）：等价于 `findBoundariesInRange(data, 0, data.size(), params)`。2.4 用它**一次扫完整条流**得到所有小块切点。

核心循环（对每个字节位置 `pos`）：

```cpp
if (pos - last_P < minT) continue;                       // ① 太短，不切
if (hash % secondD == secondD - 1) backupBreak = pos;    // ② 记一个备选切点
if (hash % mainD   == mainD   - 1) {                     // ③ 主规则命中，切
    boundaries.push_back(pos); last_P = pos; continue;
}
if (pos - last_P < maxT) continue;                       // ④ 还没到上限，继续找
// ⑤ 到上限还没主命中：用最近的备选，否则就地强制切
if (backupBreak != 0) { last_P = backupBreak; boundaries.push_back(backupBreak); }
else                  { last_P = pos;         boundaries.push_back(pos); }
```

三个关键量：

- `last_P`：上一个切点（当前块的起点）。
- `backupBreak`：最近一次“次优切点”，主规则一直不命中时用它，避免切在非内容定义的位置。
- `hash`：`sha1WindowHash(以 pos 结尾的 48 字节窗口)`。

> 返回值不含文件末尾。合成式调用方会在两端补哨兵 `0` 与 `size`，得到完整的小块起点数组。

---

## 4. 主循环 `processFileAmalgamation`（`Amalgamation.cpp:60`）

这是 2.4 的心脏。按段拆：

### 4.1 先跑小块器，建立小块起点数组

```cpp
const std::vector<std::size_t> cuts = findBoundaries(data, config.small);
std::vector<std::size_t> starts;
starts.push_back(0);
starts.insert(starts.end(), cuts.begin(), cuts.end());
starts.push_back(data.size());
const std::size_t m = starts.size() - 1;   // 小块总数
```

- `starts` 是“每个小块的起始字节偏移”：第 `j` 个小块是 `[starts[j], starts[j+1])`。
- 大块也用它表示：从第 `a` 个小块起的 `k` 个小块合成的大块，就是 `[starts[a], starts[a+k])`。
- 空流直接返回（`Amalgamation.cpp:65`）。

### 4.2 “这个大块重复吗？”——查询与缓存

```cpp
std::vector<int> isDup(m, -1);             // -1 未知 / 0 非重复 / 1 重复
queryBig(a):
    若 a+k > m      → false（构不成大块）
    若 isDup[a] 未知 → lookup(第 a 个开始的大块) → 写入 isDup[a]，并 ++gAmQueryCount
```

- `lookup` 是**只读**的“以前存过吗”，不会顺便把块存进去。
- 缓存使同一窗口最多查一次；k-fixed 的上界约为「每大块 k 次」。

### 4.3 主循环体（三种处理）

```cpp
while (i < m) {
    if (m - i < k) { emitSmallsAt(i, m - i); break; }   // 尾部不足一个大块

    for (pos = 0; pos <= k && i+pos+k <= m; ++pos) {    // 前向搜索
        a = i + pos;
        if (queryBig(a)) {                              // ① 找到重复大块
            emitSmallsAt(i, pos);                       //    先发前导小块
            emitBigAt(a, k);                            //    再发大块
            isPrevDupBig = true; i = a + k; handled = true; break;
        }
        if (isPrevDupBig) {                             // ② 离开重复区
            emitSmallsAt(i, k);                         //    连发 k 个小块
            isPrevDupBig = false; i += k; handled = true; break;
        }
    }
    if (handled) continue;

    emitBigAt(i, k);                                    // ③ 新鲜区合成大块
    isPrevDupBig = false;                               //    ← 见 Q&A Q2
    i += k;
}
```

- ① 就是论文 Fig.3 lines 3–6：找到重复大块，前面的小块零散发。
- ② 对应 lines 7–8：刚离开重复区，用 `k` 个小块把边界“收细”。
- ③ 对应 lines 9–10：大片新数据仍用大块，省元数据。

### 4.4 两种发射 `emitSmallsAt` / `emitBigAt`

```cpp
emitSmallsAt(data, starts, a, count):        // Amalgamation.cpp:33
    for j in [a, a+count): emitChunk(data, starts[j], starts[j+1]); ++gAmSmallEmitted;

emitBigAt(data, starts, a, k):               // Amalgamation.cpp:48
    emitChunk(data, starts[a], starts[a+k]); ++gAmBigChunks;
```

- 小块逐段发射；大块把 `k` 个小块**当一段连续字节**一次发射（源数据里本就相邻，无需拼接）。
- 两者都经过 `emitChunk`（`DataAndMethod.cpp:51`），由 `chunkStore` 负责去重与统计。

---

## 5. 去重存储：`chunkStore` 与 `lookup`（`ChunkStore.cpp`）

数据用两个全局容器保存（`DataAndMethod.h`）：

- `gChunkPool`：`std::deque<Chunk>`，存**唯一块**内容。用 `deque` 是因为 `push_back` 后已有元素的指针不会失效。
- `gChunkIndex`：`std::unordered_multimap<ChunkHash, Chunk*>`，哈希 → 唯一块指针。用 `multimap` 是因为哈希可能碰撞，同一哈希要有多个候选。

### `chunkStore(Chunk)`（`ChunkStore.cpp:30`）——写入并去重

1. 算内容哈希（SHA-1）。
2. 计 `gTotalChunks / gTotalBytes`（发射维度）。
3. `equal_range(hash)` 取所有同哈希候选。
4. 逐个**逐字节比较**，命中就 `++gDupChunks` 并返回既有指针（不复制）。
5. 都不命中：把内容 `move` 进池、登记索引、累加 `gUniqueBytes`。

> 为什么不能只比哈希？因为哈希可能碰撞，只看哈希会把不同内容误判为重复 → 丢数据。第 4 步的逐字节比较保证正确。

### `lookup`（`ChunkStore.cpp:71`）——只查不写

和 `chunkStore` 的查重段一样，但**只读**：命中返回指针，否则 `nullptr`；不计数、不插入。2.4 用它判断大块是否重复。详见 `docs/ChunkStore-Dedup-Walkthrough.md`。

---

## 6. 桥接层与驱动层

### `emitChunk`（`DataAndMethod.cpp:51`）

把 `[begin, end)` 切片交给 `chunkStore`，并往 `vChunks.back()` 追加一条出现记录。所有发射都走它。

### `Baseline.cpp`

- `makeAmalgamationConfig(scale)`（`:43`）：基准参数 / 4 得小块器，`k = 4×scale`。
- `processFile`（`:117`）：读整个文件到一个 `std::string`，按模式调用 `pFinder`（baseline）或 `processFileAmalgamation`（2.4）；用完即释放。
- `processDirectory`（`:136`）：收集目录下文件、**排序**（保证备份按版本先后处理），逐个处理；文件少时逐文件打印增量。
- `resolver`（`:185`）：入口分发。
- `main`（`:226`）：菜单；选 2.4 后再问大块尺寸。

---

## 7. 常见困惑 Q&A

**Q1：为什么大块不用真的把这 `k` 个小块拼起来？**
A：小块是同一整条流上相邻切出的，`k` 个连续小块在源数据里首尾相接；大块就是 `[starts[a], starts[a+k])` 一段连续字节，`lookup`/`emitChunk` 直接对它取视图即可。

**Q2：新鲜区发射大块后为什么把 `isPrevDupBig` 置 `false`？**
A：它表示“上一个大块是否是重复块”。刚发的是新数据，当然不是重复，所以 `false`。论文 Fig.3 第 10 行印成 `true`，与正文 “Regions considered fresh data … emitted as big chunks” 矛盾，按语义修正。

**Q3：前导小块（`emitSmallsAt(i, pos)`）是干什么的？**
A：当重复大块从 `pos > 0` 才开始，`[i, i+pos)` 这段还是新数据；必须先零散发掉，否则字节覆盖会出现空洞（也不能简单并进大块，否则大块边界就不是内容定义的了）。

**Q4：transition 为什么是 `k` 个而不是 `k-1` 个？**
A：论文 lookahead 为 `2k-1`，line 7 明确是 “emit k smalls”。发满 `k` 个小块保证离开重复区后的一整段都用细粒度收边，避免遗留非内容定义的大块。

**Q5：尾部为什么全部发小块？**
A：剩余不足 `k` 个小块就构不成一个大块，只能按小块发（对应 Fig.4(e) 的 straggling small chunk）。

**Q6：为什么查询次数比 2.3 多？**
A：2.3 每大块查 1 次；k-fixed 合成式最坏每大块查 `k+1` 次（前向每小块一次）。在重复数据上通常 `pos=0` 就命中，实际接近每大块 1 次；只有大片新数据才会做满前向探测。

**Q7：为什么 `k` 越大 DER 可能反而下降？**
A：`k` 越大，离开重复区时要连发的 transition 小块越多（最长 `k` 个），这些小块往往落在变更区、重复率低，于是平均块长变大但 DER 略降。这是论文 Fig.5/Fig.6 展示的参数权衡。

---

## 8. 怎么看结果

程序最后输出：

```
totalChunks     发射的块总数（含重复）
uniqueChunks    真正存储的唯一块数
duplicateChunks 命中去重的块数
totalBytes      输入总字节（= 所有文件大小之和）
uniqueBytes     实际存储字节
dedupRatio      = totalBytes / uniqueBytes  ← 论文的 DER
```

2.4 还会多一行：

```
smallChunks / bigChunks / dupBigChunks / queries / emittedSmalls
```

**比较口径**：论文关心的是“在相近 DER 下，平均块长是否更大”。平均块长可用 `totalBytes / totalChunks` 估算；只看 DER 或只看块数都不完整——两者要一起看。

---

## 9. 运行

```bash
# 构建（需要 OpenSSL）
g++ -std=c++20 -O2 -Wall -Wextra src/Baseline.cpp src/DataAndMethod.cpp \
    src/ChunkStore.cpp src/Chunker.cpp src/Amalgamation.cpp src/Hash.cpp \
    -o baseline -lcrypto

# 运行（菜单）
./baseline
# 1/3/5 → baseline；4 → 2.4(DataSet_3)；6 → 2.4(DataSet_4，合成)
# 选 4/6 后会再问大块尺寸：1≈1k / 2≈4k / 3≈16k / 4≈32k
```

生成合成备份流：

```bash
python tools/make_synthetic_backups.py     # 默认输出 Dataset/DataSet_4
```

---

## 10. 术语

| 术语 | 含义 |
| --- | --- |
| 小块 / small chunk | 用小（密）参数切出的块，合成式的基本单位 |
| 大块 / big chunk | 由恰好 `k` 个连续小块合成，长度可变、端点是内容定义的 |
| k-fixed | 大块固定由 `k` 个小块构成（论文实际采用的版本） |
| 前导小块 | 重复大块开始前的那几个小块，在该大块之前零散发掉 |
| transition | 离开重复区后连发的 `k` 个小块，用来收细边界 |
| fresh interior | 大片新数据内部，直接合成大块发射 |
| 唯一块 | 去重后真正存储的块（内容独占） |
| existence query | “这块以前存过吗”，2.4 用 `lookup` 实现 |
| DER | 输入字节 / 存储字节 |
