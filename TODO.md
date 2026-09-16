# TODO：Chunk 存储 / 去重 / 索引

本清单只记录待办，不代表已完成。设计约定：唯一块**独占存储内容**、内容哈希用 **SHA-1（OpenSSL，完整 160 位）**、索引值为 `Chunk*`、输入**逐文件流式**、**新增类**承载存储逻辑。

## 0. 前置修复（当前编译阻塞）

- [ ] 修复 `Baseline.cpp` 的 `incomplete type 'Chunk'`：`Chunk` 完整定义不能只放 `DataAndMethod.cpp`
- [ ] 删除 `DataAndMethod.h` 中的 `struct Chunk;` 前置声明
- [ ] 删除 `DataAndMethod.h` 中的 `umChunk`（`unordered_map<ull, Chunk>`），改由新类接管
- [ ] 删除 `DataAndMethod.cpp` 中的局部 `struct Chunk` 定义

## 1. 新增类 `src/ChunkStore.h` / `src/ChunkStore.cpp`

- [ ] 定义 `using ChunkHash = std::uint64_t;`
- [ ] 定义 `hashChunk(std::string_view) -> ChunkHash`（基于 SHA-1，见 `src/Hash.h`）
- [ ] 定义唯一块结构 `Chunk { ChunkHash hash; std::string data; std::size_t length; }`
- [ ] 定义出现结构 `ChunkRef { Chunk* chunk; std::size_t offset; }`
- [ ] 实现 `ChunkStore::store(std::string_view content) -> Chunk*`：算一次哈希 → 查索引 → 逐字节校验 → 命中返回既有指针并计 duplicate；否则新建并计 unique
- [ ] 实现 `ChunkStore::exists(ChunkHash) const -> bool`（供后续 bimodal existence query）
- [ ] 索引容器：`std::unordered_multimap<ChunkHash, Chunk*>`（处理哈希碰撞，值仍为 `Chunk*`）
- [ ] 唯一块池用 `std::deque<Chunk>`，保证 `push_back` 后既有 `Chunk*` 不失效
- [ ] 统计接口：`totalChunks / uniqueChunks / duplicateChunks / dedupRatio`（按字节）

## 2. 清理并接入 `DataAndMethod.h` / `DataAndMethod.cpp`

- [ ] `DataAndMethod.h` 添加 `#include "ChunkStore.h"`
- [ ] 新增全局 `inline ChunkStore gChunkStore;`
- [ ] 新增全局 `inline std::vector<std::vector<ChunkRef>> vChunks;`
- [ ] 保留 `vPosition`（边界，报告用）与 `vDiff`
- [ ] 新增 `emitChunk(const std::string& data, std::size_t begin, std::size_t end)`：切片 → `gChunkStore.store` → 追加 `ChunkRef`
- [ ] `pFinder(std::string&)` 进入时同时 `vPosition.emplace_back()` 与 `vChunks.emplace_back()`
- [ ] 主规则命中处：`emitChunk(str, last_P, p)` 后再更新 `last_P`
- [ ] `MAX_T` 备份切点处：`emitChunk(str, last_P, backupBreak)`
- [ ] `MAX_T` 强制切分处：`emitChunk(str, last_P, p)`
- [ ] 循环结束后补尾块：`emitChunk(str, last_P, str.size())`
- [ ] 处理 0 长尾块（`end <= begin` 直接跳过）
- [ ] 校验 `lengthCalculator(Chunk)` 仍引用头文件中的 `Chunk`

## 3. 输入改为逐文件流式（`Baseline.cpp`）

- [ ] 每次只保留一个文件的原始字节，分块后立即释放源缓冲
- [ ] 用一个最小的“逐文件二进制读取”循环替换按行读 `Dataset/temp.txt`
- [ ] 保证 `gChunkStore` 跨文件保留，`vChunks` 只存轻量 `ChunkRef`
- [ ] 处理完输出汇总：`totalChunks / uniqueChunks / duplicateChunks / dedupRatio`

## 4. 构建

- [ ] `CMakeLists.txt` 的 `add_executable` 增加 `src/ChunkStore.cpp`

## 5. 验证

- [ ] `clang++ -std=c++20 -Wall -Wextra src/Baseline.cpp src/DataAndMethod.cpp src/ChunkStore.cpp` 零警告
- [ ] 在 `Dataset/DataSet_1`（5 个文件）上运行并记录 SHA-1 基线：`totalChunks == 186228`（旧的 `std::hash` 基线 21158 已失效）
- [ ] 断言 `uniqueChunks + duplicateChunks == totalChunks`
- [ ] 抽查跨文件重复块只占一份 `Chunk.data`
- [ ] 抽查 `gChunkStore.exists(h)` 对已存块返回 `true`
- [ ] 确认内存：源文件处理完即释放，仅保留唯一块内容 + `ChunkRef`

## 6. 后续（本轮范围外）

- [x] 内容哈希已升级为强哈希：滑窗与内容哈希均使用 SHA-1（OpenSSL EVP，完整 160 位）
- [ ] 接入 `FileController` 那套数据集菜单/报告/SUMMARY 脚手架
- [ ] 为论文 2.3（breaking-apart）与 2.4（amalgamation）预留 existence query 接口
- [ ] 与 BSW/TTTD/TTTD-S 的分块结果做 DER 与平均块长对比
