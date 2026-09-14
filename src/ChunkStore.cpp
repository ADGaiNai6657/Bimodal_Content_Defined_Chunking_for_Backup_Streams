//
// Created by ADGaiNai6657 on 2026/9/14.
//

#include "DataAndMethod.h"

//输入一个chunk，并判断是否已经存在
//如果未存在，insert；
//如果已存在，则建立索引；
//统一返回chunk型的指针。
auto chunkStore(Chunk chunk) -> Chunk* {
    ChunkHash hash = getHashValue(getSubString(chunk));
    if (!isExist(hash)) {
        chunks.insert({hash,chunk});
        // return ;
    }
    return &chunks[hash];
}

auto isExist(ChunkHash hash) -> bool {
    if (chunks.find(hash) != chunks.end()) {
        return true;
    }
    return false;
}
