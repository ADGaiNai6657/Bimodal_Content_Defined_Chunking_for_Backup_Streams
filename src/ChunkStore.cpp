//
// Created by ADGaiNai6657 on 2026/9/14.
//

#include "DataAndMethod.h"

//输入string_viewm,返回哈希值
auto getHashValue(Chunk chunk) -> std::uint64_t {
    return std::hash<std::string_view>{}(chunk.data);
}

//输入Chunk,返回一段String
auto getString(Chunk chunk) -> std::string_view {
    return chunk.data;
}

//输入一个chunk，并判断是否已经存在
//如果未存在，insert；
//如果已存在，则建立索引；
//统一返回chunk型的指针。
auto chunkStore(Chunk chunk) -> Chunk* {
    ChunkHash hash = getHashValue(getSubString(chunk));
    if (!isExist(hash)) {
        chunks.insert({hash,chunk});
        return ;
    }
    return &chunks[hash];
}

auto isExist(ChunkHash hash) -> bool {
    if (chunks.find(hash) != chunks.end()) {
        return true;
    }
    return false;
}
