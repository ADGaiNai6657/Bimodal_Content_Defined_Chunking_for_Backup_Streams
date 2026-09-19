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
  2. 未命中，但 `isPrevDupBig`（刚离开重复区）→ 发射 `k` 个小块（lines 7–8）；
  3. 其余（大片新数据内部）→ 把 `buf[0..k-1]` 合成大块发射（line 9）。

- 查询量：k-fixed 每大块最多 `k` 次（每小块一次）；对比 2.3 拆分式的「每大块一次」。
- 论文另提 **k-var**（大块可由 1–k 或 2–k 个小块构成，每小块最多 k−1/k 次查询）作为扩展，但实测采用的是 k-fixed，故本项目只实现 k-fixed。
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
真正新增的是切片序列的索引方式（小块起点数组 `starts`）与合成式主循环。

---

## 3. 算法设计

### 3.1 数据结构

- `struct ChunkerParams { std::size_t mainD, secondD, minT, maxT, window; };`（见 `Chunker.h`）
- `struct AmalgamationConfig { ChunkerParams small; std::size_t k; };`（小块器参数 + 每大块的小块数）
- 每个文件：
  - `cuts = findBoundaries(file, config.small);`（整条流的小块切点，一次算完）
  - 展开为小块起点数组 `starts`：`starts[0] = 0`，随后是 `cuts`，末尾补 `data.size()`；
    于是共有 `m = starts.size() - 1` 个小块，第 `j` 个为 `[starts[j], starts[j+1])`。
  - `isDup[a]`：从第 `a` 个小块开始的大块是否重复的缓存（`-1` 未知 / `0` 否 / `1` 是）。
- 复用全局 `chunkStore` / `gChunkPool` / `lookup`，跨文件保留。

### 3.2 主流程伪代码（对应 Fig.3）

```text
processFile_Amalgamation(file):
    vChunks.emplace_back()                      // 本文件的出现记录组
    cuts  = findBoundaries(file, kSMALL)        // 整条流一次切成小块
    starts = [0] + cuts + [size]                // 小块起点（共 m 个小块）
    isDup = vector<int>(m, -1)                  // 大块重复缓存
    isPrevDupBig = false
    i = 0

    while i < m:
        if m - i < k:                           // 尾部不足一个大块
            emitSmalls(i, m-i); break

        handled = false
        for pos in 0 .. k:                      // 前向搜索（Fig.3 lines 2-6）
            if i + pos + k > m: break
            a = i + pos
            if queryBig(a):                     // 找到重复大块
                emitSmalls(i, pos)              // 前导小块
                emitBig(a); isPrevDupBig = true
                i = a + k; handled = true; break
            if isPrevDupBig:                    // 离开重复区（lines 7-8）
                emitSmalls(i, k); isPrevDupBig = false
                i += k; handled = true; break
        if handled: continue
        emitBig(i); isPrevDupBig = false        // 新鲜区合成大块（lines 9-10）
        i += k
```

- `queryBig(a)`：`lookup(file.substr(starts[a], starts[a+k]-starts[a])) != nullptr`；懒查询 + 缓存，避免同一窗口重复查询。
- `emitSmalls(a, count)`：把 `[starts[a], starts[a+count])` 按小块逐个 `emitChunk`。
- `emitBig(a)`：把 `[starts[a], starts[a+k])` 作为**一个**大块 `emitChunk`。因为 `k` 个小块在源数据中首尾相接，大块就是一段连续字节，无需真正拼接。

### 3.3 前导小块 / transition / 合成：三种发射的解释

| 场景 | 发射内容 | 论文位置 | 目的 |
| --- | --- | --- | --- |
| 找到重复大块，起始在 `pos > 0` | 前导 `pos` 个小块 | lines 4 | 让「进入重复区」的边界精确到小块 |
| 刚离开重复区（`isPrevDupBig`） | `k` 个小块 | lines 7 | 让「离开重复区」的边界精确到小块 |
| 大片新数据内部 | 合成 1 个大块 | lines 9 | 新数据仍用大块，省元数据 |

前导小块与 transition 都只在**重复/非重复交界处**出现，故 transition 总量受大块数约束，而不会全流碎片化。

### 3.4 尾部与对齐

- 剩余不足 `k` 个小块时无法合成完整大块，全部按小块发射（对应 Fig.4(e) 末尾“straggling small chunk”）。
- 与 2.3 的「按需区间细切」不同，2.4 的小块切点**一次性对整条流预计算**，因此同一份数据在不同备份间的小块边界完全可复现；大块边界也随之稳定。

### 3.5 查询与缓存

- 每个小块位置最多发起一次大块查询，`gAmQueryCount` 统计实际查询次数；k-fixed 的上界约为「每大块 k 次」。
- 重复文件上，前向搜索通常在 `pos = 0` 立即命中，故查询量趋近「每大块 1 次」；只有在大片新数据里才会做满 `k+1` 次前向探测。
- 查询用 `lookup`（只读、不计数、不插入），真正发射时才由 `chunkStore` 计一次发射并去重。

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

1. **Fig.3 第 10 行 `isPrevDupBig=true` 与正文/描述矛盾**：line 9 明确是 “Regions considered fresh data … emitted as big chunks”，其后应置 `false`，否则会把后续新鲜块连锁当作 transition。实现按 `false` 修正，并在源码注释中记录。
2. `isBigDup` 的语义是「**此前已存储**」：包含上次备份与本轮此前 emit 的块。不要跨文件缓存 `isDup`（store 在变）。
3. `lookup` 必须**逐字节校验**，否则哈希碰撞会把新鲜大块误判为重复。
4. 前导小块不能丢：`pos > 0` 时必须先发射 `[i, i+pos)`，否则字节覆盖出现空洞。
5. transition 应发**恰好 `k` 个**小块（论文 lookahead 为 `2k-1` 时的行为），不是 `k-1`。
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
- `src/Amalgamation.h/.cpp`：`AmalgamationConfig` / `processFileAmalgamation` / 统计量 / 复位函数。
- `DataAndMethod` 保留公共原语（`chunkStore` / `lookup` / `isExist` / `emitChunk` / SHA-1）。
  其中 baseline `pFinder` 现复用 `findBoundaries(kBaselineParams)`，只负责发射与记账，切点逻辑与 2.4/2.3 同源。
- `Baseline.cpp` 菜单：`4` = 2.4 @DataSet_3，`6` = 2.4 @DataSet_4；选后追加询问大块平均尺寸。
- 统计输出：`smallChunks / bigChunks / dupBigChunks / queries / emittedSmalls / DER`。

---

## 8. 已决问题

1. 采用 k-fixed（Fig.3），不实现 k-var；大块固定由 `k` 个连续小块构成。
2. Fig.3 第 10 行：按 `false` 修正（与正文一致）。
3. 小块切点：**整文件预计算**（与论文 summary 做法一致，切点跨备份可复现）。
4. 尾部：不足 `k` 个小块时按小块发射，保留 Fig.4(e) 的 straggling small。
5. `k`：由大块尺寸菜单派生 `k = 4 × scale`，与 2.3 尺寸口径一致。
6. 集成：做进 `Baseline` 菜单（选项 4/6），非独立可执行。
