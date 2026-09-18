//
// Amalgamation.h
// 论文 2.4「合成式（chunk amalgamation）」算法。
//
// 与 2.3 拆分式相反：合成式先用【小块器】把整条流切成小块，再把连续的 k 个小块
// 合成为一个【大块】去查询/去重。这样大块边界完全由小块切点决定（content-defined），
// 且天然允许在任意小块位置发起大块查询，粒度更灵活。
//
// 步骤（对一条数据流，对应论文 Fig.3 的 k-fixed 版本）：
//   1) 用小块器把流切成 m 个小块；
//   2) 在窗口 buf[0..2k-1] 上做前向搜索，找第一个重复的大块（k 个连续小块）：
//        a. 找到（位置 pos）-> 先把 pos 个前导小块按小块发射，再把该大块按大块发射；
//        b. 没找到，但刚离开重复区（isPrevDupBig）-> 发射 k 个小块（transition）；
//        c. 其余（大片新数据内部）-> 把 k 个小块合成一个大块发射。
//   3) 重复上述步骤，剩余不足 k 个小块时全部按小块发射（尾块）。
//
// 名词：
//   小块 / small：用小块器切出的块，是合成式的基本单位。
//   大块 / big  ：由恰好 k 个连续小块合成，长度可变但块端点是内容定义的。
//   k-fixed    ：大块固定由 k 个小块构成（论文实际采用的版本）。
//

#ifndef BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_AMALGAMATION_H
#define BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_AMALGAMATION_H

#include <cstddef>
#include <string>

#include "Chunker.h"

// 2.4 合成式参数。
struct AmalgamationConfig {
    ChunkerParams small; // 小块器参数：先跑它得到整条流的小块切点。
    std::size_t k;       // 每个大块由 k 个连续小块合成（k-fixed）。
};

// 对一条数据流执行合成式分块，并把块发射给 ChunkStore（去重）。
// 内部会为本条流追加一组 vPosition / vChunks 记录。
auto processFileAmalgamation(const std::string& data, const AmalgamationConfig& config) -> void;

// ---------------------------------------------------------------------------
// 统计量（供程序末尾汇总，解释算法行为）
// ---------------------------------------------------------------------------
inline std::size_t gAmSmallChunks = 0;   // 小块器切出的小块总数。
inline std::size_t gAmBigChunks = 0;     // 合成并发射的大块数（含重复）。
inline std::size_t gAmDupBigChunks = 0;  // 其中命中重复的大块数。
inline std::size_t gAmQueryCount = 0;    // 大块存在性查询次数（论文：每大块最多 k 次）。
inline std::size_t gAmSmallEmitted = 0;  // 以小块的粒度单独发射的小块数（transition + 尾块）。

// 把上面的统计量清零（每次运行 2.4 之前调用）。
auto resetAmalgamationStats() -> void;

#endif //BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_AMALGAMATION_H
