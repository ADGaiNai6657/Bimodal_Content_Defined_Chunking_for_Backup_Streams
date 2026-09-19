//
// Created by ADGaiNai6657 on 2026/9/13.
//
// DataAndMethod.h
// 本文件承担两件事：
//   1) TTTD 分块算法的参数、数据结构与函数声明；
//   2) 去重存储（ChunkStore）的全局状态、内容索引与统计量。
// 数据流：Baseline 逐文件整读 -> pFinder 产生边界 -> emitChunk 切片发射
//         -> chunkStore 去重 -> 写入 gChunkPool/gChunkIndex。
// 其中 gChunkPool/gChunkIndex 跨文件保留，是全局去重的基础；vChunks 只记录轻量引用。

#ifndef BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_DATA_H
#define BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_DATA_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <memory>
#include <unordered_map>
#include <deque>
#include <vector>

#include "Chunker.h"
#include "Hash.h"

struct Chunk;

// 内容哈希：完整 160 位 SHA-1 摘要。
using ChunkHash = Sha1Digest;

using ull = unsigned long long;

// TTTD 算法参数（迁移自 TTTD_Experiments/TTTD_Algorithm.cpp）：
// 原先这里的 CONST_VALUE_MAIN_D / CONST_VALUE_SECOND_D / MIN_T / MAX_T / MY_LENGTH
// 已统一为 Chunker.h 中的 kBaselineParams（见 Chunker.h），baseline pFinder 与
// 2.4 的小块器共用同一份定义，避免两处各写一遍。

// 每个文件一组的切点位置（文件内偏移），仅用于报告与调试。
inline std::vector<std::vector<ull>> vPosition;
// 与 vPosition 对应，存放相邻切点之差（即块长），便于统计块长分布。
inline std::vector<std::vector<ull>> vDiff;

// ------------- ------------------ 哈希与切片 --------------------------------

// 内容哈希入口：对一段字节计算完整 SHA-1 摘要作为 ChunkHash。
auto getChunkHash(std::string_view str) -> ChunkHash;

// 取“结束于 end”的定长窗口 [end-length, end)；流首不足 length 时取 [0, end)。
auto getSubString(std::string_view str, std::size_t end, std::size_t length) -> std::string_view;

// 取整个 Chunk 的内容视图，用于计算哈希与逐字节比较。
auto getSubString(const Chunk &chunk) -> std::string_view;

// ------------------------------- 分块与发射 --------------------------------

// 计算每个文件相邻边界的差值（块长）。
auto diffCalculator(const std::vector<std::vector<ull>>& v) -> std::vector<std::vector<ull>>;

// 对一批数据流依次分块（每个元素对应一个文件/一条流）。
auto pFinder(const std::vector<std::string>& str) -> void;

// TTTD 核心：对单个数据流产生边界，并把边界间的数据发射给 ChunkStore。
auto pFinder(const std::string& str) -> void;

// 旧版冒烟测试路径：按行读 Dataset/temp.txt。
auto strPushback(std::vector<std::string>& str) -> void;

// 把 data 的 [begin, end) 切片交给 chunkStore 去重，并追加一条出现记录到 vChunks。
auto emitChunk(const std::string& data, std::size_t begin, std::size_t end) -> void;

// ------------------------------- 去重存储 ----------------------------------

// 内容去重入口：命中则返回既有唯一块，未命中则新建；由 emitChunk 调用。
auto chunkStore(Chunk chunk) -> Chunk*;

// 存在性查询：供后续 bimodal（2.3/2.4）算法判断候选块是否已存。
auto isExist(const ChunkHash &hash) -> bool;

// 精确存在性查询：命中返回唯一块指针，否则 nullptr；只读，不改索引与统计。
// 2.3 拆分式用它判断大块是否重复（逐字节校验，避免哈希碰撞误判）。
auto lookup(std::string_view content) -> Chunk*;

// 唯一块：独占一份内容，代表全局去重后真正需要存储的数据。
struct Chunk {
    ChunkHash hash;        // 内容哈希。
    std::string data;      // 内容副本（源缓冲释放后仍可用）。
    std::size_t length;    // content.size()，缓存以避免反复调用。
};

// 一次“块出现”事件：指向某个唯一块，并记录它在源文件中的起始偏移。
struct ChunkRef {
    Chunk* chunk;
    std::size_t offset;
};

// 唯一块池：deque 保证 push_back 后既有的 Chunk* 不失效。
inline std::deque<Chunk> gChunkPool;
// 内容索引：一个哈希可对应多个 Chunk*（碰撞候选），命中后再逐字节确认。
inline std::unordered_multimap<ChunkHash, Chunk*, Sha1DigestHash> gChunkIndex;
inline std::size_t gTotalChunks = 0;  // 发射的块总数（含重复）。
inline std::size_t gDupChunks = 0;    // 命中去重的块数。
inline std::size_t gTotalBytes = 0;   // 发射的总字节数（等于输入字节数）。
inline std::size_t gUniqueBytes = 0;  // 唯一块占用的总字节数。
// 每个文件一组出现记录，元素为轻量 ChunkRef（不持有内容）。
inline std::vector<std::vector<ChunkRef>> vChunks;

#endif //BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_DATA_H
