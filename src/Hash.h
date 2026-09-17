//
// Created by ADGaiNai6657 on 2026/9/16.
//
// Hash.h
// 基于 OpenSSL EVP 的 SHA-1 封装。承担两件事：
//   1) 完整 160 位内容摘要，作为去重索引 ChunkHash；
//   2) 截断 64 位窗口哈希，供 TTTD 取模判断边界。
// 统一放这里，避免 DataAndMethod / ChunkStore 直接依赖 OpenSSL。

#ifndef BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_HASH_H
#define BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_HASH_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// SHA-1 摘要：固定 160 位 = 20 字节。作为去重索引的 key。
using Sha1Digest = std::array<std::uint8_t, 20>;

// 内容哈希入口：对一段字节计算完整 SHA-1 摘要（160 位）。
// data 为零拷贝视图；返回 20 字节摘要，作为唯一块的内容指纹。
Sha1Digest sha1(std::string_view data);

// 窗口哈希：对定长滑动窗口取 SHA-1，再截断为 64 位整数。
// 取摘要前 8 字节按大端拼接（最高位在前），供 pFinder 的 % D 判断切点；
// 字节序固定，保证跨平台可复现。
auto sha1WindowHash(std::string_view data) -> std::uint64_t;

// Sha1Digest 的无序容器哈希函数：把 20 字节摘要压成一个 size_t 桶号。
// std::unordered_multimap 默认没有 std::array 的 std::hash，故需显式提供。
struct Sha1DigestHash {
    // 取摘要前 8 字节直接作为桶号；真碰撞交给容器的链地址法处理。
    auto operator()(const Sha1Digest& digest) const noexcept -> std::size_t;
};

#endif //BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_HASH_H
