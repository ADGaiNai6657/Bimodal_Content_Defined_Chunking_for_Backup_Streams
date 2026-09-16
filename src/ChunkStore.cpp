//
// Created by ADGaiNai6657 on 2026/9/14.
//

//
// ChunkStore 的实现：内容去重与索引。
// 只有唯一块写入 gChunkPool；重复块只返回既有指针，因此块内容是独占的。
//

#include <utility>

#include "DataAndMethod.h"

/**
 * 内容去重的唯一入口：对一个待发射的块做「查重 -> 记录 -> 返回唯一块指针」。
 *
 * 参数按值传入（接口约定为 Chunk），因此调用方把切片构造成 Chunk 后由本函数接管；
 * 未命中时直接 std::move 进池，避免再复制一次内容。
 *
 * 处理步骤（与下面的行内注释一一对应）：
 *   1. 对块内容算哈希（getSubString 得到零拷贝视图）；
 *   2. 无论命中与否，都把它计为一次“发射”，并累加发射字节数；
 *   3. 用 equal_range 取出所有同哈希候选（哈希碰撞的块会排在一起）；
 *   4. 逐个做「长度 + 逐字节内容」确认，命中即复用既有唯一块；
 *   5. 全部候选都不匹配，才把内容移入唯一块池并登记索引。
 *
 * 恒等式：调用 N 次后，gTotalChunks == gUniqueChunks + gDupChunks，
 * 且 gTotalBytes == 输入总字节数、gUniqueBytes == 池中内容总字节数。
 */
auto chunkStore(Chunk chunk) -> Chunk* {
    // 第 1 步：内容哈希。同一段字节在任何文件、任何位置都应得到相同哈希。
    const ChunkHash hash = getChunkHash(getSubString(chunk));

    // 第 2 步：统计“发射”维度（含重复块）。
    ++gTotalChunks;
    gTotalBytes += chunk.length;

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

    // 第 5 步：未命中，新建唯一块。move 进 deque 后取稳定指针再登记。
    gChunkPool.push_back(std::move(chunk)); // 未命中：参数移入池，无额外拷贝。
    Chunk* stored = &gChunkPool.back();     // deque 保证该指针长期有效。
    gChunkIndex.emplace(hash, stored);      // 登记到内容索引。
    gUniqueBytes += stored->length;         // 唯一块内容计入“实际存储字节”。
    return stored;
}

/**
 * 存在性查询：给定内容哈希，判断是否已经有对应唯一块。
 * 目前供去重统计/测试使用，后续 bimodal（论文 2.3/2.4）用它做“是否已存在”判断。
 * 注意：只看哈希、不校验内容，因此可能因碰撞产生假阳性；精确查询需再逐字节比较。
 */
auto isExist(const ChunkHash &hash) -> bool {
    return gChunkIndex.contains(hash);
}
