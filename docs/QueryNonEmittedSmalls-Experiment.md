# `queryNonEmittedSmalls` 开关的作用原理与实验报告

> 对象：论文 2.4 k-var 变体中的“非发射小块”查询开关。
> 实现：`AmalgamationConfig::queryNonEmittedSmalls`（`src/Amalgamation.h:38`）、
> 精确存在性集合 `gSmallPresence`（`src/Amalgamation.cpp:64`）。
> 数据集：`Dataset/DataSet_4`（8 × 32 MiB 合成集中变更备份流）。
> 一句话结论：**开关确实能降低唯一存储字节、提升 DER（本次实验 +1.0% ~ 1.6%），
> 但其作用是间接的（通过更细粒度的重对齐与索引累积），代价是查询量与块数大幅上升。**

---

## 1. 开关是什么

`AmalgamationConfig` 上的一个布尔量，默认 `false`，**只对 k-var 生效**。驱动层在
k-var 模式下会把它置为 `true`（`src/Baseline.cpp:327`），因此它是 k-var 的默认行为。

它控制“一个以前出现过、但从未作为独立块被发射过的小块”，是否也算作“已存在”。
对应的数据结构是 `gSmallPresence`：

- 类型：`std::unordered_set<ChunkHash, Sha1DigestHash>`（`src/Amalgamation.cpp:64`）。
- 写入：k-var 每消费一批小块（单独发射、transition、或作为大块的一部分）后，
  都调用 `rememberSmalls(...)` 把它们的 SHA-1 内容哈希登记进去
  （`src/Amalgamation.cpp:258 / 261 / 281 / 285`）。
- 读取：k-var 的 `isBigStored` 在**候选长度为 1** 且开关打开时查一次
  （`src/Amalgamation.cpp:232-233`）。
- 生命周期：跨文件全局，`resetAmalgamationStats()` 时清空（`src/Amalgamation.cpp:299`）。

判定逻辑（k-var，`src/Amalgamation.cpp:224-236`）：

```cpp
if (len == 1 && queryNonEmitted) {
    return lookup(content) != nullptr                       // 主索引：发射过的大块/小块
        || gSmallPresence.contains(getChunkHash(content));  // 额外：只是“出现过”的小块
}
return lookup(content) != nullptr;
```

---

## 2. 作用原理详解

### 2.1 两个“存在性”来源

| | `gChunkIndex`（主索引） | `gSmallPresence`（本开关） |
| --- | --- | --- |
| 内容 | 真正发射过的唯一块（含内容） | 所有被消费过小块的 SHA-1 哈希 |
| 是否含“被大块吞掉的小块” | 否 | 是 |
| 判定方式 | 哈希 + 逐字节比较，可返回 `Chunk*` | 仅哈希（SHA-1 碰撞可忽略），不返回内容 |
| 启用条件 | 始终 | 仅 k-var 且 `queryNonEmittedSmalls == true` |
| 生命周期 | 跨文件全局 | 跨文件全局，`resetAmalgamationStats()` 清空 |

关键差别：主索引里存的是“大块整体”。某个小块若只作为大块的一部分被存入，
**它自己并不在索引里**，后续 `lookup(该小块)` 会返回 `nullptr`。`gSmallPresence`
正是为了记住这些“影子小块”而存在。

### 2.2 为什么“单个小块命中”能起作用（重对齐机制）

底层小块器是内容定义的（content-defined）：未改动区域的切点会与上一份备份一致，
改动区域则会在有限距离内 **resync**。大块则是 `k` 个连续小块。

**关闭开关时的失效路径**：

1. 进入一段未改区域后，若当前游标的小块分组相位与上一份备份“存大块时”的相位不一致，
   那么 `lookup(bigStart, len)` 在 `len = k..2` 上可能全部落空（拼接的内容/长度对不上）。
2. 若此时 `prevBigWasDup == false`，算法按“新鲜数据内部”处理，把接下来 `k` 个小块
   合成一个**新大块**存入，游标一次跨 `k` 个小块。
3. 这一次跨越可能“跨过”了真正对齐的重复大块起点（overshoot）。在被重新对齐之前，
   这段本该命中的重复区被当成新数据存了下来。

**打开开关后的路径**：

1. resync 之后，未改区域每个小块的字节与上一份备份完全一致，其哈希早已由
   `rememberSmalls` 进入 `gSmallPresence`。
2. 于是搜索在 **lookahead = 0** 就得到 `isBigStored(bigStart, 1) == true`，
   在**第一个重复小块处**重新锚定，而不必等 `k` 个小块凑齐。
3. 命中分支发射这 1 个小块，置 `prevBigWasDup = true`、`nextSmall = bigStart + 1`
   （`src/Amalgamation.cpp:255-266`）。下一轮从下一个小块继续搜索，很快就能对齐到
   主索引里的重复大块并真正命中。
4. 效果：把每个变更点“误存为新数据的前缀”从最多约 `k` 个小块压缩到接近 0。

同时，被单独发射的小块会进入主索引，于是**后续备份**可以在小块粒度直接去重；
`gSmallPresence` 也随 `rememberSmalls` 继续增长，使细粒度识别能力逐份累积。

### 2.3 为什么它是“间接”的

- 开关真正改变判定的，只有 **`lookup` 未命中、但 `presence` 命中** 的情形。这类块随后被
  `emitBigAt(..., len = 1)` 交给 `chunkStore`（`src/DataAndMethod.cpp:43-50` →
  `src/ChunkStore.cpp:30-56`）。因为主索引里并无它的独立副本，**首次仍按新唯一块存入**
  （`src/ChunkStore.cpp:51-54`），不会凭空产生一次去重命中。
- 收益来自级联：若干个“仅 presence”的判定改变会改变后续游标的相位，使后续更长的大块
  得以对齐并真正命中；被单独发射的小块又进入主索引，让后续备份可在小块粒度去重。
- 因此该开关更像一个**更细粒度的重同步 / 重对齐触发器**，而非“直接复用被吞小块”。

---

## 3. 实验设置

- **隔离**：用 `git archive HEAD` 把当前提交导出到临时目录，`Dataset/DataSet_4` 以软链接
  指向原数据；**未改动工作仓库**，实验后删除临时目录（`HEAD` 始终保持 `568c08a`，
  `git status` 干净）。
- **临时插桩**（仅实验副本）：
  - 环境变量 `AM_QUERY_NON_EMITTED=0/1` 覆盖开关，便于同一二进制做 ON/OFF 对比；
  - 新增 `gAmPresenceHits`（presence 命中数）与 `gAmPresenceOnlyHits`
    （其中 `lookup` 未命中、仅 presence 命中的数）两个计数器。
  - 插桩只读计数，不改变算法判定。
- **运行**：k-var + `DataSet_4`，规模选项 1（`k = 4`）与 2（`k = 16`），ON/OFF 各一次；
  单线程 Release 构建，每次约 36 ~ 41 s。

---

## 4. 结果

### 4.1 规模 1（`k = 4`）

| 指标 | OFF | ON（默认） | 变化 |
| --- | ---: | ---: | ---: |
| dedupRatio (DER) | 7.8882 | **7.9691** | **+1.03%** |
| uniqueBytes | 34,030,188 | **33,684,338** | **−345,850 (−1.02%)** |
| uniqueChunks | 36,983 | 46,047 | +24.5% |
| duplicateChunks | 246,446 | 325,349 | +32.0% |
| totalChunks | 283,429 | 371,396 | +31.0% |
| queries | 837,792 | 1,224,482 | +46.2% |
| emittedSmalls | 3,833 | 14,058 | +266.8% |
| presenceHits | 0 | 122,275 | — |
| presenceOnlyHits | 0 | 2,381 | — |

### 4.2 规模 2（`k = 16`）

| 指标 | OFF | ON（默认） | 变化 |
| --- | ---: | ---: | ---: |
| dedupRatio (DER) | 7.8155 | **7.9369** | **+1.55%** |
| uniqueBytes | 34,346,357 | **33,821,262** | **−525,095 (−1.53%)** |
| uniqueChunks | 23,992 | 49,781 | +107.5% |
| duplicateChunks | 120,312 | 325,314 | +170.4% |
| totalChunks | 144,304 | 375,095 | +159.9% |
| queries | 3,308,031 | 6,753,845 | +104.2% |
| emittedSmalls | 16,429 | 41,320 | +151.5% |
| presenceHits | 0 | 287,009 | — |
| presenceOnlyHits | 0 | 2,795 | — |

> `dedupRatio` = 输入字节 / 唯一块字节（即论文 DER，见 `src/Baseline.cpp:361-371`）；
> `presenceHits / presenceOnlyHits` 为本次实验新增计数。

---

## 5. 分析

1. **开关有效，但收益随 `k` 增大更明显**：DER 提升 `k = 4` 为 +1.03%、`k = 16` 为 +1.55%。
   这与 2.2 的相位分析一致——`k` 越大，相位不齐 / overshoot 的“误存前缀”代价越大，
   重对齐收益越大。
2. **收益是间接的、由少量种子放大**：真正改变判定的只有 `presenceOnlyHits`
   （`k = 4`：2,381；`k = 16`：2,795），却引发块数、重复块数的大幅变化。绝大多数
   `presenceHits`（约 98%）对应的 `lookup` 本就命中，属于冗余命中。
3. **代价显著**：查询量约 +0.5 ~ 1 倍，发射小块数 +1.5 ~ 2.7 倍，总块数 +30% ~ 160%
   （元数据与查询开销上升）。
4. **趋于小块粒度去重**：ON 在 `k = 16` 下的 `totalChunks`(375,095) / `duplicateChunks`
   (325,314) 与 `k = 4` 下几乎相同，说明开关把行为收敛到接近“小块粒度的去重”，
   对 `k` 不再敏感。

---

## 6. 结论与取舍

- 开关**不是**“直接复用被大块吞掉的小块”，而是以“单小块曾出现过”为线索，
  在变更边界处做**更细粒度的重对齐**，并通过把小块写入主索引实现跨备份的细粒度去重。
- 本次实验净收益：唯一存储字节减少约 **1.0% ~ 1.5%**，DER 提升约 **1.0% ~ 1.6%**；
  代价：查询与块数大幅增加。
- 与论文定位一致：k-var 用 Bloom filter 做“非发射小块”的细粒度识别；本实现用
  **精确集合**替代 Bloom filter（无假阳性、更占内存）。该特性只出现在 k-var，
  k-fixed 不使用，故默认值在配置层为 `false`，由驱动层在 k-var 模式置 `true`。
- 若场景更看重元数据 / 查询成本，可通过 `AmalgamationConfig::queryNonEmittedSmalls = false`
  关闭该特性。

---

## 7. 复现步骤

```bash
# 1) 隔离副本（不污染工作仓库）
EXP=/tmp/amalg-exp && rm -rf "$EXP" && mkdir -p "$EXP"
git archive HEAD CMakeLists.txt src | tar -x -C "$EXP"
mkdir -p "$EXP/Dataset" && ln -s "$PWD/Dataset/DataSet_4" "$EXP/Dataset/DataSet_4"

# 2) 在临时副本中插桩：环境变量覆盖开关 + presence 计数（见第 3 节）
# 3) 构建
cmake -S "$EXP" -B "$EXP/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$EXP/build" -j8

# 4) ON/OFF 对比（规模选项 1 = k4；2 = k16）
BIN="$EXP/build/Bimodal_Content_Defined_Chunking_for_Backup_Streams"
( cd "$EXP/build" && printf '8\n1\n' | AM_QUERY_NON_EMITTED=0 "$BIN" )   # OFF, k=4
( cd "$EXP/build" && printf '8\n1\n' | AM_QUERY_NON_EMITTED=1 "$BIN" )   # ON,  k=4
( cd "$EXP/build" && printf '8\n2\n' | AM_QUERY_NON_EMITTED=0 "$BIN" )   # OFF, k=16
( cd "$EXP/build" && printf '8\n2\n' | AM_QUERY_NON_EMITTED=1 "$BIN" )   # ON,  k=16
```

---

## 附录：相关代码位置（提交 `568c08a`）

| 位置 | 说明 |
| --- | --- |
| `src/Amalgamation.h:38` | `queryNonEmittedSmalls` 开关定义 |
| `src/Amalgamation.cpp:64` | `gSmallPresence` 精确存在性集合 |
| `src/Amalgamation.cpp:67-84` | `rememberSmall` / `rememberSmalls` 写入 |
| `src/Amalgamation.cpp:224-236` | k-var `isBigStored`，`len == 1` 时查 presence |
| `src/Amalgamation.cpp:255-266` | k-var 命中分支：发射前导小块 + 大块、推进游标 |
| `src/Amalgamation.cpp:299` | `resetAmalgamationStats` 清空集合 |
| `src/Baseline.cpp:327` | 驱动层在 k-var 模式置开关为 `true` |
| `src/ChunkStore.cpp:39-56` | `chunkStore`：真正的去重裁判（命中复用 / 未命中新建） |
