# 拆分式（Breaking-Apart）算法设计（论文 2.3）

> 目标：在已有 baseline（TTTD 分块 + ChunkStore 去重）之上，复现论文第 2.3 节的 breaking-apart 算法。
> 状态：设计文档，尚未实现。
> 命名：本项目中底层分块器统称 **TTTD**（Twin Threshold Two Divisors）。

---

## 1. 论文 2.3 要点（对照 Fig.1 / Fig.2）

- 先做**大块级**扫描（baseline 参数），对每个大块查询是否存在（`isBigDup`）。
- 决策规则（Fig.1）：

  1. 大块是重复 → 按大块 emit；
  2. 否则，若**前一块**或**后一块**是重复 → 把该大块范围**按小块重切**并 emit（change region / transition）；
  3. 否则（内部新鲜区）→ 按大块 emit。

- 判定「后一块是否重复」需要 **1 个大块的前瞻**（lookahead）；查询量约 **每大块 1 次**。
- 论文实验取 **N = R = D = 1**：1 个非重复大块邻接 1 个重复大块时，只重切 1 块。
- 小块平均块长约为大块的 **1/4 ~ 1/8**。
- 允许同一段字节既以大块、又以小块形式存储（inline dedup 不重写已发大块）。

---

## 2. 现状与差距

| 需要的能力 | 现状 | 差距 |
| --- | --- | --- |
| 参数化分块器 | `pFinder` 用全局常量 `CONST_VALUE_*`，且**边算边界边 emit** | 需要纯函数 `findBoundaries(str, params) -> vector<size_t>`，大/小两套参数各跑一次 |
| 精确存在性查询 | `isExist(const ChunkHash&)` 只看哈希、不校验内容 | `isBigDup` 必须精确，建议加 `lookup(string_view) -> Chunk*`（hash + `equal_range` + 逐字节，不插入、不计数） |
| 两种粒度 emit | `emitChunk` 写全局 `vChunks.back()` | 可复用，但要保证每个文件先 `vChunks.emplace_back()` |
| 内容哈希 | 已是完整 SHA-1（`getChunkHash`） | 碰撞可忽略，`isBigDup` 可直接用哈希 |
| 窗口哈希 | `sha1WindowHash`（48B 截断 64 位） | 供两套分块器复用 |

关键结论：**把 `pFinder` 拆成「纯分块」+「发射」两步**，是 2.3 的架构前提。baseline 仍可保留「算完就 emit」，但大/小两套必须能独立得到边界序列。

---

## 3. 算法设计

### 3.1 数据结构

- `struct ChunkerParams { std::size_t mainD, secondD, halfSecondD, minT, maxT, switchP, window; };`
- `std::vector<std::size_t> findBoundaries(std::string_view data, const ChunkerParams&);`
  （纯函数，返回切点，末尾追加 `data.size()` 作为哨兵）
- 每个文件：
  - `bigBounds = findBoundaries(file, kBIG);`
  - `smallBounds = findBoundaries(file, kSMALL);`（**预先对整文件算一次**，见 3.4）
  - 大块列表：`bigRange[i] = [prevBig, bigBounds[i])`，共 `bigBounds.size()` 块。
- 每个大块一个 `dup[i]`（精确 `lookup`），懒查询 + 缓存。
- 复用全局 `chunkStore` / `gChunkPool` / `isExist`，跨文件保留。

### 3.2 主流程伪代码

```text
processFile_BreakingApart(file):
    vChunks.emplace_back()                      // 本文件的出现记录组
    big = findBoundaries(file, kBIG)            // 大块切点（只先算这一遍）
    n = big.size()                              // 大块数
    dup = vector<bool>(n)                       // 懒查询缓存
    prevDup = false

    for i in 0 .. n-1:
        (b0, b1) = range_of_big(i, big)         // i==0 时 b0=0
        cur  = queryDup(i, b0, b1)              // 精确 lookup，缓存进 dup[i]
        next = (i+1 < n) ? queryDup(i+1, ...) : false

        if cur:
            emitBig(b0, b1)                     // 重复大块：原样存
            prevDup = true
        else if prevDup || next:
            small = findBoundariesInRange(file, b0, b1, kSMALL) // 现算，仅此区间
            emitSmalls(b0, b1, small)           // transition：按 small 切点重切
            prevDup = false
        else:
            emitBig(b0, b1)                     // 内部新鲜区
            prevDup = false                     // ← 见陷阱 5.1
```

- `queryDup(i)`：`lookup(file.substr(b0, b1-b0)) != nullptr`；**必须在 emit 之前查询**（查询反映此前所有历史存储）。
- `emitBig(b0,b1)`：`emitChunk(file, b0, b1)`。
- `emitSmalls(b0,b1,small)`：取所有满足 `b0 < s < b1` 的 `small` 切点，把 `[b0,b1)` 切成若干段逐个 `emitChunk`；边缘残余（`b0` 到第一个内部切点）也算一小块。

### 3.3 前瞻与「每大块一次查询」

- 每个大块只算一次哈希、查一次，结果缓存，`next` 复用为下一轮的 `cur`，满足论文「one query per large chunk」。
- 查询时机细节：`next` 在 emit 当前块之前算出，所以「当前块与下一块内容相同」时 `next` 会滞后一拍。该情形下 `cur` 也是非重复，两条分支最终都走 `emitBig`，行为一致，无正确性影响。

### 3.4 小块切点来源（重要设计选择）

- **当前采用**：按需区间细切。只在判定 change region 时，对 `[b0, b1)` 调用
  `findBoundariesInRange(data, b0, b1, kSMALL)`。窗口取自整段 `data`（可越过 `b0` 左端），
  `min/max` 从 `b0` 起算。
  - 优点：不重复扫描整文件；首个备份 / 无变更文件完全不跑小块器。
  - 代价：小块切点从 `b0` 起算 `min`，与“整文件全局对齐”的切点略有差异，结果数值会变。
- 备选（论文 summary 做法）：整文件预计算小块切点，rechunk 时取区间内的点。
  - 与论文一致、切点全局对齐，但每个文件都要多跑一整遍小块器。

---

## 4. 参数选择

- 大块器 = 现有 `CONST_VALUE_*`（平均 ≈ 965 B）。
- 小块器 = 大块参数整体缩 k 倍，k 取 4 或 8（论文「4–8×」）。窗口 `window` 需 `≤ minT`。

| k | minT | mainD | secondD | half | maxT | switchP |
| --- | --- | --- | --- | --- | --- | --- |
| 4 | 115 | 135 | 67 | 33 | 700 | 400 |
| 8 | 57 | 67 | 33 | 16 | 350 | 200 |

建议先跑 k=4、k=8 两点，看平均块长 / DER 曲线（对应论文 Fig.6 趋势）。窗口可继续用 48，也可同比缩小；论文窗口 12–48 均可。

---

## 5. 关键陷阱

1. **Fig.1 第 6 行的 `isPrevBigDup=true` 与正文/Fig.2 矛盾**：内部新鲜块之后应为 `false`，否则会连锁把后续块都当 transition。建议按 `false` 实现，并在注释里记录这一取舍。
2. `isBigDup` 的语义是「**此前已存储**」：包括上次备份与本轮此前 emit 的块。不要跨文件缓存 `dup`（store 在变）。
3. `lookup` 必须**逐字节校验**：否则哈希碰撞会把新鲜块误判为重复 → 错判 transition。
4. 边缘小块：`[b0,b1)` 两端可能落在某个小块内部，要按「从 b0 到下一个内部切点」切，不要丢弃。
5. 字节覆盖不重不漏：transition 用小块、其余用大块，二者不重叠；断言 `sum(emit 长度) == 文件大小`。
6. 重复大块的 emit：调用 `chunkStore` 后返回既有指针，不新增 `uniqueBytes`（现有 store 已处理）。
7. 论文允许同一数据双份存储（大块 + 小块），DER 会略受影响，这是预期行为。

---

## 6. 验证方法

- **单文件**：第一个备份应无重复 → 除尾部外全是大块，`DER=1`，块数 ≈ 大块数（可与 baseline 对比）。
- **多版本顺序处理**（DataSet_2 五个 emacs 版本）：`DER>1`，且**平均块长明显大于 baseline 在同等 DER 时**，即论文核心结论。
- 断言：`gTotalChunks == gChunkPool.size() + gDupChunks`；总 emit 字节 = 输入字节；查询次数 ≈ 大块数。
- 与论文 Fig.6 做**定性**对照（不同 k 的曲线形状）。

---

## 7. 模块规划

- 新增 `src/BreakingApart.h` / `src/BreakingApart.cpp`，放 `ChunkerParams`、`findBoundaries`、`processFile_BreakingApart`。
- `DataAndMethod` 保留公共原语（`chunkStore` / `lookup` / `isExist` / `emitChunk` / SHA-1）。
- `Baseline.cpp` 菜单加选项 3「拆分式（2.3）」，数据集选择复用现有逻辑。
- 统计输出扩展：`bigChunks / dupBigChunks / queries / avgBigLen / avgSmallLen / DER`。

---

## 8. 待定问题

1. transition 判据固定 `N=R=D=1`，还是做成参数？
2. Fig.1 第 6 行按 `false` 修正，还是严格照抄伪代码？
3. 小块切点采用「整文件预计算」（推荐）还是「区域内重跑」？
4. k 先取 4，还是 4+8 都跑？
5. 2.3 做进 `Baseline` 菜单，还是独立可执行？
