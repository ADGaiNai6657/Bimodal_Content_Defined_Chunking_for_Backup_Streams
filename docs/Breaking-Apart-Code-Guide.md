# 拆分式（2.3）代码导读

> 目的：把 `src/BreakingApart.*` 及相关改动的代码讲清楚，读代码时不必再猜。
> 配套：算法设计见 `docs/Breaking-Apart-Design.md`；查重细节见 `docs/ChunkStore-Dedup-Walkthrough.md`。

---

## 0. 一句话理解整个流程

```
读一个文件(一次“备份”)的原始字节
   │
   ├─ 用【大块器】把它切成若干“大块”          ← TTTD 分块（只跑这一遍）
   │
   └─ 逐个大块问一句：这块以前存过吗？
        存过  → 原样发出去（会去重，不新增存储）
        没存过，但左边或右边那块存过 → 说明这里是“变更边缘”，
                                      只对这一小段用【小块器】现切后发出
        其余  → 原样发出（大片新数据仍用大块，省元数据）
```

就这么三步。下面把每一步拆开讲。

---

## 1. 阅读顺序（按数据流）

1. `src/BreakingApart.h` —— 先认类型：`ChunkerParams`、`BreakingApartConfig`。
2. `src/BreakingApart.cpp:46 findBoundaries` —— 最基础的“怎么切”。
3. `src/BreakingApart.cpp:97 processFileBreakingApart` —— 2.3 的主循环（最核心）。
4. `src/BreakingApart.cpp:17 emitSmalls` —— 被主循环调用的“小块细切”。
5. `src/ChunkStore.cpp:30 chunkStore` / `:71 lookup` —— 块存哪、怎么查。
6. `src/DataAndMethod.cpp:51 emitChunk` —— 主循环与存储之间的桥。
7. `src/Baseline.cpp:85 processFile` / `:104 processDirectory` / `:148 resolver` —— 驱动层。
8. `tools/make_synthetic_backups.py` —— 合成测试数据。

---

## 2. 先认类型（`BreakingApart.h`）

### `ChunkerParams`（`BreakingApart.h:22`）
一套 TTTD 分块参数。以前这些是写死的常量，现在抽出来，好让“大块器”和“小块器”共用同一套切分代码：

| 字段 | 含义 | 影响 |
| --- | --- | --- |
| `mainD` | 主除数 | `hash % mainD == mainD-1` 命中就切；越大块越少 |
| `secondD` | 备份除数 | 主规则长时间不命中时的兜底候选 |
| `minT` | 最小块长 | 小于它不切 |
| `maxT` | 最大块长 | 到这个长度强制切 |
| `window` | 滑动窗口字节数 | 取哈希的窗口大小，本项目 48 |

> 直觉：`mainD` 控制“平均块长”。`minT + mainD` 约等于平均块长（比如 460+540≈1000）。

### `BreakingApartConfig`（`BreakingApart.h:31`）
就是 `{ big, small }` 两个 `ChunkerParams`。2.3 需要两套参数：大块器负责主体，小块器负责“变更边缘”。

### 统计量（`BreakingApart.h:46-50`）
全局计数器，用来解释算法行为：
- `gBaBigChunks`：共看了多少个大块。
- `gBaDupBigChunks`：其中多少个是重复的。
- `gBaQueryCount`：查询了多少次（论文要求约等于大块数）。
- `gBaRechunkRegions`：有多少个 change region 被小块重切。
- `gBaSmallChunks`：重切共产生多少个小块。

---

## 3. `findBoundaries` / `findBoundariesInRange`：纯分块，不存不发射

**职责**：给一段字节，返回切点位置（升序），**不碰全局存储**。
- `findBoundariesInRange(data, b0, b1, params)`（`BreakingApart.cpp:46`）：只扫 `[b0, b1]`，但窗口取自整段 `data`（可越过 `b0` 左端），`min/max` 从 `b0` 起算。
- `findBoundaries(data, params)`（`BreakingApart.cpp:89`）：等价于 `findBoundariesInRange(data, 0, data.size(), params)`。

后者用于“大块/整文件”，前者用于“按需细切某个 change region”。

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

> 为什么返回值不含文件末尾？因为“块”是由相邻切点定义的，最后一段由调用方补上（见 `processFileBreakingApart` 的 `n-1` 处理）。

---

## 4. 主循环 `processFileBreakingApart`（`BreakingApart.cpp:97`）

这是 2.3 的心脏。按行拆：

### 4.1 只先算大块切点

```cpp
const std::vector<std::size_t> big = findBoundaries(data, config.big);
```

- `big` 把文件切成 `n = big.size() + 1` 个大块。
- **不再**开局就把整文件的小块切点算出来。小块切点只在 §4.3 情形② 真正需要时，才用 `findBoundariesInRange` 对该区间现算（见 §7 Q1）。

`rangeOf(i)` 把第 `i` 个大块换算成字节区间 `[b0, b1)`：
- 第 0 块从 0 开始；
- 最后一块到 `data.size()` 结束。

### 4.2 “重复吗？”——查询与缓存

```cpp
std::vector<int> dup(n, -1);           // -1 未知 / 0 非重复 / 1 重复
query(i): 若 dup[i] 未知 → lookup(第 i 块内容) → 写入 dup[i]
```

- `lookup` 是**只读**的“以前存过吗”，不会顺便把块存进去。
- 每个大块只查一次（`dup` 缓存），所以 `gBaQueryCount ≈ gBaBigChunks`，符合论文“每大块一次查询”。

### 4.3 三种处理（主循环体）

```cpp
cur  = query(i);      // 当前大块是否重复
next = query(i + 1);  // 下一个大块是否重复（前瞻 1 块）

if (cur)                          // ① 自己就是重复的
    emitChunk(data, b0, b1);      //    原样按大块发出（会去重）
else if (prevDup || next) {       // ② 自己不重复，但挨着一个重复块
    auto small = findBoundariesInRange(data, b0, b1, config.small); // 只在这一段现算
    emitSmalls(data, b0, b1, small);  // 这是“变更边缘”，用小块细切
} else                            // ③ 大片新数据内部
    emitChunk(data, b0, b1);      //    仍按大块发出
```

- `prevDup`：上一个大块是不是重复块（状态变量）。
- 情形 ② 就是论文说的 transition / change region：块本身是新的，但它夹在重复数据和新数据之间，细切能让“新旧边界”更精确，下一次备份更容易命中重复。
- 情形 ③ 后面把 `prevDup` 置 `false`（论文 Fig.1 第 6 行印成 `true`，与正文矛盾，这里按语义修正）。

### 4.4 小块细切 `emitSmalls`（`BreakingApart.cpp:17`）

把 `[b0, b1)` 这一段按 `small` 里落在其中的切点切开，逐段发射：

```
b0 ──s1──s2──s3── b1
 └─段1─┘└段2┘└段3┘└尾段┘
```

- 只取满足 `b0 < s < b1` 的小块切点。
- 两端不足一小块的残余也各自成一段，不会丢字节。

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
和 `chunkStore` 的查重段一样，但**只读**：命中返回指针，否则 `nullptr`；不计数、不插入。2.3 用它判断大块是否重复。

---

## 6. 桥接层与驱动层

### `emitChunk`（`DataAndMethod.cpp:51`）
把 `[begin, end)` 切片交给 `chunkStore`，并往 `vChunks.back()` 追加一条出现记录。所有发射都走它。

### `Baseline.cpp`
- `processFile`（`:85`）：读整个文件到一个 `std::string`，按模式调用 `pFinder`（baseline）或 `processFileBreakingApart`（2.3）；用完即释放。
- `processDirectory`（`:104`）：收集目录下文件、**排序**（保证备份按版本先后处理），逐个处理；文件少时逐文件打印增量。
- `resolver`（`:148`）：入口分发。
- 选 2.3 后会再问大块尺寸；`makeBreakingConfig(scale)` 用基准参数乘 `scale` 得到大块器，小块器 = 大块器 `/ 4`。

---

## 7. 常见困惑 Q&A

**Q1：小块切点是怎么算的？为什么不在开局把整文件都算好？**
A：只在真正要细切的 change region 内现算（`findBoundariesInRange`），其余地方不跑小块器。好处是省时间：无需重复扫描整文件（尤其首个备份/无变更文件完全不跑小块）。窗口仍取自整段 `data`，所以靠近 `b0` 的切点会受 `b0` 之前字节影响，切点依旧“内容定义”、同内容可复现。代价是它从 `b0` 起算 `min`，与“整文件全局对齐”的切点略有差异，结果数值会变。

**Q2：为什么 `next` 在发射当前块之前就查询？**
A：为了满足“每大块一次查询”。代价是当第 `i+1` 块与第 `i` 块内容相同时，`next` 会滞后一拍；但那种情况两者最终都走“按大块发射”，行为一致，不影响正确性。

**Q3：`isPrevBigDup` 之后为什么置 `false`？**
A：它是“上一个大块是否是重复块”。刚发的是新数据，当然不是重复，所以 `false`。论文 Fig.1 第 6 行的 `true` 与正文/Fig.2 矛盾，按语义修正。

**Q4：`ChunkRef` 的 `offset` 是什么？**
A：该块在**源文件**中的起始偏移，用于回溯位置；它不表示“块内偏移”。

**Q5：为什么重复大块还要调用 `emitChunk`？**
A：要让统计与索引状态一致（`chunkStore` 会识别为重复并复用既有指针），但不会新增唯一字节。

**Q6：为什么平均块长有时变小？**
A：change region 多时，小块占比高，会把平均块长拉低。这在“变更分散”的数据（如 emacs）上尤其明显。

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

2.3 还会多一行：
```
bigChunks / dupBigChunks / queries / rechunkRegions / smallChunks
```

**比较口径**：论文关心的是“在相近 DER 下，平均块长是否更大”。只看 DER 或只看块数都不完整——两者要一起看。

---

## 9. 运行

```bash
# 构建（需要 OpenSSL）
g++ -std=c++20 -O2 -Wall -Wextra src/Baseline.cpp src/DataAndMethod.cpp \
    src/ChunkStore.cpp src/BreakingApart.cpp src/Hash.cpp -o baseline -lcrypto

# 运行（菜单）
./baseline
# 1/3/5 → baseline；4 → 2.3(DataSet_3)；6 → 2.3(DataSet_4，合成)
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
| 大块 / big chunk | 用 baseline TTTD 参数切出的块 |
| 小块 / small chunk | 用更密参数切出的块，仅用于 change region |
| change region / transition | 重复数据与新数据的交界处，会被小块细切 |
| 唯一块 | 去重后真正存储的块（内容独占） |
| existence query | “这块以前存过吗”，2.3 用 `lookup` 实现 |
| DER | 输入字节 / 存储字节 |
