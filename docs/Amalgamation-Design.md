# 合成式（Chunk Amalgamation）算法设计（论文 2.4）

> 目标：在已有 baseline（TTTD 分块 + ChunkStore 去重）与公共 `Chunker` 之上，复现论文第 2.4 节的 chunk amalgamation 算法。
> 状态：**已实现**（`src/Amalgamation.h/.cpp`）。实现细节与最新行号见 `docs/Amalgamation-Code-Guide.md`。
> 命名：本项目中底层分块器统称 **TTTD**（Twin Threshold Two Divisors）。

---

## 1. 论文 2.4 要点（对照 Fig.3 / Fig.4）

- 与 2.3 相反：**先跑小块器**（更细），再把连续的 `k` 个小块**合成**为大块去查询/去重。
- “**k-fixed**”：大块固定由 `k` 个连续小块构成。其长度可变，但块端点是内容定义的。论文实际采用的即 Fig.3 的 k-fixed 版本。
- 决策规则（Fig.3）：

  1. 前向搜索 `pos = 0..k`，找第一个重复大块 `buf[pos .. pos+k-1]`；
     命中 → 先发射前导小块 `buf[0..pos-1]`，再发射该大块（lines 3–6）；
  2. 未命中，但刚离开重复区（Fig.3 中的 `isPrevDupBig`，实现名 `prevBigWasDup`）→ 发射 `k` 个小块（lines 7–8）；
  3. 其余（大片新数据内部）→ 把 `buf[0..k-1]` 合成大块发射（line 9）。

- 查询量：k-fixed 每大块最多 `k` 次（每小块一次）；对比 2.3 拆分式的「每大块一次」。
- 论文另提 **k-var**（大块可由 1–k 或 2–k 个小块构成，每小块最多 k−1/k 次查询）作为扩展。`amalgamation` 分支实现 k-fixed；本分支（`amalgamation_k-var`）额外实现 k-var（见 3.6）。
- lookahead buffer 为 `2k-1` 个小块，使 **transition 区不会超过 k 个小块**（不过论文也允许把 lookahead 扩到 `3k-1`、transition 扩到 `2k-1`）。
- 允许同一段字节既以小块、又以大块形式存储：transition 处的小块可能落在重复字节范围内，从而提升后续备份的命中概率。
- 可选的重同步切点（resynchronization cut-point）与多级备份切点用于限制两条流长时间失步，本项目未启用。

---

## 2. 现状与差距

| 需要的能力 | 现状 | 差距 |
| --- | --- | --- |
| 参数化分块器 | 已抽出 `src/Chunker.h`（`kBaselineParams` / `ChunkerParams` / `findBoundaries` / `findBoundariesInRange` / `deriveSmallParams`）；baseline `pFinder` 也已改为调用 `findBoundaries` | 无需新增，直接复用；2.4 只需一次**全流**小块分块 |
| 精确存在性查询 | `lookup(std::string_view) -> Chunk*` 已具备（只读 + 逐字节） | 无需新增 |
| 两种粒度 emit | `emitChunk` 写全局 `vChunks.back()` | 可复用，但要保证每个文件先 `vChunks.emplace_back()` |
| 内容哈希 | 已是完整 SHA-1（`getChunkHash`） | 碰撞可忽略，大块重复判定走 `lookup` 即可 |
| 窗口哈希 | `sha1WindowHash`（48B 截断 64 位） | 供小块器复用 |

关键结论：2.4 不需要新的存储原语，只需「**小块器全流分块**」+「按 `k` 滑动窗口做大块查询与决策」。
真正新增的是切片序列的索引方式（小块起点数组 `smallStart`）与合成式主循环。

---

## 3. 算法设计

### 3.1 数据结构

- `struct ChunkerParams { std::size_t mainD, secondD, minT, maxT, window; };`（见 `Chunker.h`）
- `struct AmalgamationConfig { ChunkerParams small; std::size_t k; };`（小块器参数 + 每大块的小块数；实现内把 `config.k` 读入局部 `k_smallsPerBig` 使用）
- 每个文件：
  - `smallCuts = findBoundaries(file, config.small);`（整条流的小块切点，一次算完）
  - 展开为小块起点数组 `smallStart`：`smallStart[0] = 0`，随后是 `smallCuts`，末尾补 `data.size()`；
    于是共有 `smallCount = smallStart.size() - 1` 个小块，第 `j` 个为 `[smallStart[j], smallStart[j+1])`。
  - `dupCache[firstSmall]`：从第 `firstSmall` 个小块开始的大块是否重复的缓存（`-1` 未知 / `0` 否 / `1` 是）。
- 复用全局 `chunkStore` / `gChunkPool` / `lookup`，跨文件保留。

### 3.2 主流程伪代码（对应 Fig.3）

```text
processFile_Amalgamation(file):
    vChunks.emplace_back()                              // 本文件的出现记录组
    smallCuts  = findBoundaries(file, small)            // 整条流一次切成小块
    smallStart = [0] + smallCuts + [size]               // 小块起点（共 smallCount 个小块）
    dupCache   = vector<int>(smallCount, -1)            // 大块重复缓存
    prevBigWasDup = false
    nextSmall = 0

    while nextSmall < smallCount:
        if smallCount - nextSmall < k:       // 尾部不足一个大块
            emitSmalls(nextSmall, smallCount-nextSmall); break

        emitted = false
        for lookahead in 0 .. k:             // 前向搜索（Fig.3 lines 2-6）
            if nextSmall + lookahead + k > smallCount: break
            bigStart = nextSmall + lookahead
            if isBigStored(bigStart):                   // 找到重复大块
                emitSmalls(nextSmall, lookahead)        // 前导小块
                emitBig(bigStart); prevBigWasDup = true
                nextSmall = bigStart + k; emitted = true; break
            if prevBigWasDup:                           // 离开重复区（lines 7-8）
                emitSmalls(nextSmall, k); prevBigWasDup = false
                nextSmall += k; emitted = true; break
        if emitted: continue
        emitBig(nextSmall); prevBigWasDup = false       // 新鲜区合成大块（lines 9-10）
        nextSmall += k
```

- `isBigStored(firstSmall)`：`lookup(file.substr(smallStart[firstSmall], smallStart[firstSmall+k]-smallStart[firstSmall])) != nullptr`；懒查询 + 缓存，避免同一窗口重复查询。
- `emitSmalls(firstSmall, numSmalls)`：把 `[smallStart[firstSmall], smallStart[firstSmall+numSmalls])` 按小块逐个 `emitChunk`。
- `emitBig(firstSmall)`：把 `[smallStart[firstSmall], smallStart[firstSmall+k])` 作为**一个**大块 `emitChunk`。因为 `k` 个小块在源数据中首尾相接，大块就是一段连续字节，无需真正拼接。

### 3.3 前导小块 / transition / 合成：三种发射的解释

| 场景 | 发射内容 | 论文位置 | 目的 |
| --- | --- | --- | --- |
| 找到重复大块，起始在 `lookahead > 0` | 前导 `lookahead` 个小块 | lines 4 | 让「进入重复区」的边界精确到小块 |
| 刚离开重复区（`prevBigWasDup`） | `k` 个小块 | lines 7 | 让「离开重复区」的边界精确到小块 |
| 大片新数据内部 | 合成 1 个大块 | lines 9 | 新数据仍用大块，省元数据 |

前导小块与 transition 都只在**重复/非重复交界处**出现，故 transition 总量受大块数约束，而不会全流碎片化。

### 3.4 尾部与对齐

- 剩余不足 `k` 个小块时无法合成完整大块，全部按小块发射（对应 Fig.4(e) 末尾“straggling small chunk”）。
- 与 2.3 的「按需区间细切」不同，2.4 的小块切点**一次性对整条流预计算**，因此同一份数据在不同备份间的小块边界完全可复现；大块边界也随之稳定。

### 3.5 查询与缓存

- 每个小块位置最多发起一次大块查询，`gAmQueryCount` 统计实际查询次数；k-fixed 的上界约为「每大块 k 次」。
- 重复文件上，前向搜索通常在 `lookahead = 0` 立即命中，故查询量趋近「每大块 1 次」；只有在大片新数据里才会做满 `k+1` 次前向探测。
- 查询用 `lookup`（只读、不计数、不插入），真正发射时才由 `chunkStore` 计一次发射并去重。

### 3.6 k-var 变体（本分支实现）

论文把“大块固定 k 个小块”放宽为 **1..k 个连续小块的任意组合**，称为 k-var：

- **搜索**：在每个起点 `bigStart = nextSmall + lookahead`，把长度从 `maxLen` 递减到 1 依次查询，**优先匹配更长的大块**；`lookahead` 最多 `k-1`，故上界约 `k(k-1)` 次查询/大块（论文 Fig.5 的 k-var 曲线）。
- **发射**：命中 `[bigStart, bigStart+len)` 后，前面 `lookahead` 个小块零散发，再发该大块；`nextSmall` 跳到 `bigStart + len`。
- **新鲜区**：没找到任何重复大块时，把接下来的 `min(k, 剩余)` 个小块合成一个大块；因为大块允许变短，**不再需要 k-fixed 的尾块特殊处理**。
- **非发射小块**：论文 k-var 还会查询“以前出现过、但只作为某个大块组成部分被发射”的小块（论文用 Bloom filter）。实现里用精确集合 `gSmallPresence` 保存这些小块的 SHA-1 内容哈希，`len == 1` 时把 `lookup(...) != nullptr || gSmallPresence.contains(...)` 作为存在判据；由 `AmalgamationConfig::queryNonEmittedSmalls` 控制开关。
- **代价**：查询次数约为 k-fixed 的 k 倍；精确集合比 Bloom filter 更占内存，但不会因假阳性把新鲜块误判为重复。

对应实现：`src/Amalgamation.cpp` 的 `processFileAmalgamationKVar`；菜单项 7/8 触发。

#### 3.6.1 参数 `queryNonEmittedSmalls` 详解

这是 `AmalgamationConfig` 上的布尔开关（默认 `false`），**只对 k-var 生效**，决定“要不要把曾经出现过、但从未被单独存储过的小块也算作已存在”。

**它解决的问题**：主索引 `gChunkIndex` 只登记**真正发射过的唯一块**。作为某个大块组成部分被“吞咽”的小块，内容随大块整体存储，小块本身没有记录；后续备份再出现同一个小块时，`lookup(该小块)` 会返回 `nullptr`，主索引根本不认识它。

**打开后多做什么**：k-var 额外维护“出现过的小块”哈希集合 `gSmallPresence`：

- 每消费一批小块（无论单独发射，还是作为大块的一部分），都调用 `rememberSmalls(...)` 把它们的 SHA-1 内容哈希写进去；
- 存在性查询里，**仅当候选长度为 1** 时才查该集合：

```cpp
if (len == 1 && queryNonEmitted) {
    return lookup(content) != nullptr                    // 主索引：发射过的大块/小块
        || gSmallPresence.contains(getChunkHash(content)); // 额外：只是“出现过”的小块
}
return lookup(content) != nullptr;
```

之所以只在 `len == 1` 生效：该集合只保存单个小块的哈希，无法回答“某几个小块拼起来是否出现过”；多小块的拼接是否存过，仍由主索引负责。

**行为影响（更细的重复边界）**：关闭时，k-var 只在“变长拼接能在主索引里查到”时才切分；打开后，即使各长大块都查不到，只要该位置的那个**小块**以前出现过，就能判定为重复，从而在这里切开并进入 `prevBigWasDup` / transition 逻辑。这正是论文 k-var 用 Bloom filter 做的“更细粒度变更分界”。

> 例：备份1 中 `S0..S7` 被吞成两个大块 `B0=S0..S3`、`B1=S4..S7`，主索引只有 `B0/B1`，`gSmallPresence` 有 `h(S0)..h(S7)`。备份2 在 `S5` 内部小改：关闭时 `lookup(S5)` 不认识它；打开后 `gSmallPresence` 认出 `h(S5)`，可在此切出长度 1 的“重复大块”，边界更贴合真实变更点。

| | `gChunkIndex`（主索引） | `gSmallPresence`（本开关） |
| --- | --- | --- |
| 内容 | 发射过的唯一块（含内容） | 只保存“出现过的小块”的 SHA-1 哈希 |
| 判等 | 哈希 + 逐字节比较 | 仅哈希（SHA-1 碰撞可忽略） |
| 能否用于发射/复用内容 | 能（返回 `Chunk*`） | 不能（无内容）；长度 1 发射用的是当前字节，故无需 |
| 启用条件 | 始终 | 仅 k-var 且 `queryNonEmittedSmalls == true` |
| 生命周期 | 跨文件全局 | 跨文件全局，`resetAmalgamationStats()` 时清空 |

**代价与默认值**：需要保存所有出现过小块哈希（精确集合比 Bloom filter 占内存，且每消费一个小块多算一次 SHA-1）；开启后会在更细粒度上切分，可能把大块切碎（平均块长下降）。该特性只出现在论文标注的 k-var 里，k-fixed 不用，故默认关闭；`Baseline.cpp` 在 k-var 模式下置为 `true`。

**与统计的关系**：每次候选查询都 `++gAmQueryCount`；开关不改变候选数，只改变 `len == 1` 的命中率。关闭时 `gSmallPresence` 不参与任何判断。

---

## 4. 参数选择

- 小块器 = 基准 TTTD 参数整体缩 4 倍（当前固定 `kAmSmallDivisor = 4`）：

  | 参数 | 基准 | 小块（/4） |
  | --- | ---: | ---: |
  | `mainD` | 540 | 135 |
  | `secondD` | 270 | 67 |
  | `minT` | 460 | 115 |
  | `maxT` | 2800 | 700 |
  | `window` | 48 | 48 |

  小块平均块长 ≈ `115 + 135 ≈ 250` B。
- 大块 = `k = 4 × scale` 个小块，故大块平均块长 ≈ `scale × 1k`：

  | 菜单 | `scale` | `k` | 大块平均 |
  | --- | ---: | ---: | ---: |
  | 1 | 1 | 4 | ≈ 1k |
  | 2 | 4 | 16 | ≈ 4k |
  | 3 | 16 | 64 | ≈ 16k |
  | 4 | 32 | 128 | ≈ 32k |

  这样大块尺寸菜单与 2.3 保持同一口径，便于横向比较。

---

## 5. 关键陷阱

1. **Fig.3 第 10 行 `prevBigWasDup=true` 与正文/描述矛盾**：line 9 明确是 “Regions considered fresh data … emitted as big chunks”，其后应置 `false`，否则会把后续新鲜块连锁当作 transition。实现按 `false` 修正，并在源码注释中记录。
2. `isBigDup` 的语义是「**此前已存储**」：包含上次备份与本轮此前 emit 的块。不要跨文件缓存 `dupCache`（store 在变）。
3. `lookup` 必须**逐字节校验**，否则哈希碰撞会把新鲜大块误判为重复。
4. 前导小块不能丢：`lookahead > 0` 时必须先发射 `[nextSmall, nextSmall+lookahead)`，否则字节覆盖出现空洞。
5. transition 应发**恰好 `k` 个**小块（论文 lookahead 为 `2k-1` 时的行为），不是少一个。
6. 字节覆盖不重不漏：三种发射互不重叠，断言 `sum(emit 长度) == 文件大小`。
7. 重复大块的 emit：调用 `chunkStore` 后返回既有指针，不新增 `uniqueBytes`（store 已处理）。
8. `k` 越大，每次「离开重复区」要发射的 transition 小块越多（最长 `k` 个小块），平均块长升高但 DER 可能下降；这是参数权衡，不是缺陷。

---

## 6. 验证方法

- **单文件**：第一个备份应无重复 → 除前导/transition/尾部外几乎全是大块。
- **重复文件**：完全相同的第二份应几乎全部命中 `dupBigChunks`，不新增唯一块。
- 不变量断言：
  - `gTotalChunks == gChunkPool.size() + gDupChunks`
  - 相邻发射区间首尾相接，`sum(emit 长度) == 文件大小`（不重不漏）
  - `gAmQueryCount` 与「大块数 × (1..k)」同量级
- **多版本顺序处理**：`DataSet_3`（未压缩 tar，五版本）为真实数据；`DataSet_4`（合成集中变更）用于验证 P1/P2 成立时的收益。
- 与论文 Fig.5 / Fig.6 做**定性**对照（不同 `k`、不同大块尺寸下「DER vs 平均块长」的曲线形状）。

---

## 7. 模块规划

- `src/Chunker.h/.cpp`：公共纯分块器 `ChunkerParams` / `findBoundariesInRange` / `findBoundaries` / `deriveSmallParams`。
- `src/Amalgamation.h/.cpp`：`AmalgamationConfig` / `processFileAmalgamation`（k-fixed）/ `processFileAmalgamationKVar`（k-var）/ 统计量 / 复位函数。
- `DataAndMethod` 保留公共原语（`chunkStore` / `lookup` / `isExist` / `emitChunk` / SHA-1）。
  其中 baseline `pFinder` 现复用 `findBoundaries(kBaselineParams)`，只负责发射与记账，切点逻辑与 2.4/2.3 同源。
- `Baseline.cpp` 菜单：`4`/`6` = k-fixed @DataSet_3/4，`7`/`8` = k-var @DataSet_3/4；选后追加询问大块平均尺寸。
- 统计输出：`smallChunks / bigChunks / dupBigChunks / queries / emittedSmalls / DER`。

---

## 8. 已决问题

1. 实现两种变体：k-fixed（Fig.3，选项 4/6）与 k-var（1..k 变长大块，选项 7/8）。
2. Fig.3 第 10 行：按 `false` 修正（与正文一致）。
3. 小块切点：**整文件预计算**（与论文 summary 做法一致，切点跨备份可复现）。
4. 尾部：k-fixed 不足 `k` 个小块时按小块发射（Fig.4(e) straggling small）；k-var 让最后一个大块自动变短。
5. `k`：由大块尺寸菜单派生 `k = 4 × scale`，与 2.3 尺寸口径一致。
6. 集成：做进 `Baseline` 菜单（k-fixed 4/6，k-var 7/8），非独立可执行。
7. k-var 的非发射小块查询：用精确 SHA-1 集合替代论文的 Bloom filter，默认开启（`queryNonEmittedSmalls`）。
