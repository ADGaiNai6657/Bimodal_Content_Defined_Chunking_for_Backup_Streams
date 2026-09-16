# ChunkStore 查重逻辑详解

> 对应源码：`src/ChunkStore.cpp:38-48`（`chunkStore` 的查重循环）
> 相关定义：`src/DataAndMethod.h`（`Chunk` / `gChunkPool` / `gChunkIndex`）

本文讲解 `chunkStore` 中「取出同哈希候选 → 逐字节确认 → 复用或新建」这段查重代码。它解决一件事：**如何在一个可能发生哈希碰撞的内容索引里，精确判断一个块是不是已经存过。**

---

## 1. 前提：索引的结构

```cpp
inline std::deque<Chunk> gChunkPool;                            // 唯一块池
inline std::unordered_multimap<ChunkHash, Chunk*> gChunkIndex;  // 内容索引
```

- key = `ChunkHash`（内容哈希），value = `Chunk*`（指向 `gChunkPool` 中的唯一块）。
- 使用 **`unordered_multimap`** 而不是 `unordered_map`，是因为哈希不唯一：不同内容可能算出同一个哈希（**碰撞**），它们必须以「相同 key 的多个 entry」共存。
- 标准保证：`unordered_multimap` 中 key 等价的元素在迭代时**相邻排列**，因此 `equal_range` 能一次性圈出全部候选。
- 为什么池用 `std::deque`：`push_back` 后既有元素的指针/引用**不失效**，所以 `gChunkIndex` 里保存的 `Chunk*` 长期安全。

---

## 2. 逐行拆解

```cpp
// 第 3 步：取出全部同哈希候选。索引是 multimap，故可能有多个。
const auto range = gChunkIndex.equal_range(hash);
for (auto it = range.first; it != range.second; ++it) {
    Chunk* candidate = it->second;
    // 第 4 步：逐字节确认。哈希相同不足以判定重复，必须内容完全一致。
    if (candidate->length == chunk.length && candidate->data == chunk.data) {
        ++gDupChunks;            // 逐字节确认后才算重复。
        return candidate;        // 复用唯一块，不再复制内容。
    }
    // 长度/内容不符 -> 只是哈希碰撞，继续看下一个候选。
}
```

### 2.1 `equal_range(hash)`

- 返回 `std::pair<iterator, iterator>`，即区间 `[first, second)`。
- 它圈出**所有** `key == hash` 的 entry，而不是像 `find` 那样只给一个。
- 若一个都没有，`range.first == range.second`，循环体一次都不执行。
- `const auto`：`range`（迭代器对）本身不再被修改。

### 2.2 `for (auto it = range.first; it != range.second; ++it)`

- 标准的半开区间遍历，`it` 依次指向每个候选 entry。
- 每个 entry 中：`it->first` 是哈希（此处都等于 `hash`），`it->second` 是 `Chunk*`。

### 2.3 `Chunk* candidate = it->second;`

- 取出候选唯一块指针，便于后续两次比较与最终返回。

### 2.4 `candidate->length == chunk.length && candidate->data == chunk.data`

两段式比较，先粗后精：

| 顺序 | 表达式 | 开销 | 作用 |
| --- | --- | --- | --- |
| 1 | `candidate->length == chunk.length` | O(1) | 缓存长度粗筛，长度不同立即排除，避免昂贵的整串比较 |
| 2 | `candidate->data == chunk.data` | O(L) | `std::string::operator==` 先比 size、相等后再 `memcmp` 内容，给出精确判定 |

- `&&` 短路：长度不等时右边不执行。
- 长度检查对 `std::string::operator==` 逻辑上是冗余的（它内部也会比长度），但显式写出来可省一次函数调用，也表达「先粗筛、后精比」的意图。

### 2.5 命中分支

```cpp
++gDupChunks;            // 计数器
return candidate;        // 复用唯一块
```

- 命中：只增加重复计数，直接返回既有唯一块指针，**不插入、不复制内容**。因此重复块在 `gChunkPool` 中不占第二份内存。
- 返回的指针稳定：它指向 `deque` 元素，后续 `push_back` 不会使其失效。

### 2.6 未命中分支（循环结束后）

```cpp
// 第 5 步：未命中，新建唯一块。move 进 deque 后取稳定指针再登记。
gChunkPool.push_back(std::move(chunk)); // 参数移入池，无额外拷贝
Chunk* stored = &gChunkPool.back();
gChunkIndex.emplace(hash, stored);
gUniqueBytes += stored->length;
return stored;
```

- 遍历完整个候选组都没有匹配 → 只是哈希碰撞，真正的新块。
- `std::move(chunk)` 把按值传入的参数内容移入池，避免再一次复制。
- 取 `deque` 尾部稳定指针登记进索引；唯一块内容计入 `gUniqueBytes`。

---

## 3. 为什么必须逐字节校验

`SHA-1` 虽是密码学哈希，理论上仍存在碰撞可能（不同内容映射到同一 160 位摘要）。若只用哈希判重：

- 一旦碰撞，就会把**内容不同的块**误判为重复 → 该块不会被存储 → **数据丢失**。

因此采用「哈希分桶 + 内容确认」两段式：

- 哈希把候选缩小到极少数（平均 1 个）；
- 逐字节比较给出**精确**判定，保证去重正确性。

代价：命中时多一次最多 `length` 字节的 `memcmp`。块平均约 965 B，且绝大多数情况下候选数为 1，开销可接受。

> 备注：内容哈希已升级为 SHA-1（完整 160 位），与论文一致；仍配合逐字节校验保证去重正确性。

---

## 4. 复杂度

- `equal_range`：平均 O(1) + O(k)，k 为同哈希候选数（实践中基本为 0 或 1）。
- 循环：每个候选先 O(1) 比长度，必要时 O(L) 比内容。
- 整体平均近似 **O(L)**（L = 块长），与已存块总量无关，满足论文「每单位输入常数时间」的要求。

---

## 5. 为什么用 `equal_range` 而不是 `find`

`find(hash)` 只返回一个候选。如果那一项恰好是碰撞的「别的内容」，而真正的重复块位于同 key 的下一个 entry，`find` 会漏判 → 重复存储。

`equal_range` 保证检查完整个碰撞组，语义正确。

---

## 6. 接口 B 的副作用

`chunkStore` 签名为 `chunkStore(Chunk chunk)`（**按值**），调用方 `emitChunk` 中 `data.substr(...)` 已复制一份内容到 `chunk`：

- **命中**：`chunk` 仅被比较，函数返回时销毁 → 这次复制被浪费（重复块白拷贝一次）。
- **未命中**：`std::move(chunk)` 移入池，无额外拷贝。

这是选择接口 B 的固有代价；当前数据规模可接受。若日后追求性能，可改为 `const Chunk&`（新建时再复制）或改回 `std::string_view` 接口。

---

## 7. 不变量（可用于测试断言）

对 `chunkStore` 调用 N 次后：

- `gTotalChunks == gChunkPool.size() + gDupChunks`
- `gTotalBytes == 输入总字节数`
- `gUniqueBytes == gChunkPool` 中所有块的内容总字节数
- `dedupRatio == gTotalBytes / gUniqueBytes`（即论文中的 DER）

---

## 附：相关源码位置

| 内容 | 位置 |
| --- | --- |
| 索引 / 池 / 计数器的定义 | `src/DataAndMethod.h` |
| `chunkStore` 查重主体 | `src/ChunkStore.cpp:30-56` |
| 查重循环（本文重点） | `src/ChunkStore.cpp:38-48` |
| `isExist` 存在性查询 | `src/ChunkStore.cpp:63-65` |
| `emitChunk` 发射切片 | `src/DataAndMethod.cpp` |
