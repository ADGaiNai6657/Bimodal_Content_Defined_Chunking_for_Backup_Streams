//
// Created by ADGaiNai6657 on 2026/9/14.
//

#ifndef BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_CHUNKSTORE_H
#define BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_CHUNKSTORE_H

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <string>
#include <memory>
#include <unordered_map>
#include <deque>

struct Chunk;

using ChunkHash = std::uint64_t;

auto getHashValue(std::string_view content) -> std::uint64_t;

auto getSubString(Chunk chunk) -> std::string_view;

auto chunkStore(std::string_view content) -> Chunk*;

auto isExist(ChunkHash hash) -> bool;

struct Chunk {
    ChunkHash hash;
    std::string data;
    std::size_t length;
};

struct ChunkRef {
    Chunk* chunk;
    std::size_t offset;
};

class ChunkStore {
};

inline std::unordered_map<ChunkHash,Chunk> chunks;

#endif //BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_CHUNKSTORE_H
