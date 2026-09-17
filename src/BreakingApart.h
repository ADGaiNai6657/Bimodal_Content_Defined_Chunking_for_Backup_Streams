//
// BreakingApart.h
// 论文 2.3「拆分式（breaking-apart）」算法。
//
// 思路：先用大块器（TTTD 参数）扫一遍，对每个大块做精确存在性查询；
//   1) 重复大块         -> 原样按大块发射；
//   2) 非重复但与重复相邻 -> 在该大块范围内按小块器切点重切后发射（change region）；
//   3) 其余非重复内部块   -> 原样按大块发射。
// 判定「后一块是否重复」需要 1 个大块的前瞻，查询量约每大块一次。
//

#ifndef BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_BREAKINGAPART_H
#define BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_BREAKINGAPART_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// TTTD 的参数集合：把原来写死在 pFinder 里的常量抽出来，
// 使大块器与小块器可以共用同一套分块逻辑。
struct ChunkerParams {
    std::size_t mainD;   // 主除数。
    std::size_t secondD; // 备份除数。
    std::size_t minT;    // 最小块长。
    std::size_t maxT;    // 最大块长（强制切分）。
    std::size_t window;  // 滑动窗口字节数。
};

// 大/小块器的参数组合。小块器一般由大块器按 k 倍缩小得到。
struct BreakingApartConfig {
    ChunkerParams big;
    ChunkerParams small;
};

// 纯函数分块器：返回切点位置（升序，不含文件末尾），不发射、不改全局状态。
auto findBoundaries(std::string_view data, const ChunkerParams& params) -> std::vector<std::size_t>;

// 由大块参数派生小块参数：各尺寸除以 k（窗口保持不变）。
auto deriveSmallParams(const ChunkerParams& big, std::size_t k) -> ChunkerParams;

// 对一条数据流执行拆分式分块，并把块发射给 ChunkStore（去重）。
auto processFileBreakingApart(const std::string& data, const BreakingApartConfig& config) -> void;

// 拆分式的统计量（供汇总输出）。
inline std::size_t gBaBigChunks = 0;      // 大块总数。
inline std::size_t gBaDupBigChunks = 0;   // 其中判为重复的大块数。
inline std::size_t gBaQueryCount = 0;     // 存在性查询次数。
inline std::size_t gBaRechunkRegions = 0; // 被小块重切的 transition 区域数。
inline std::size_t gBaSmallChunks = 0;    // 重切产生的小块数。

auto resetBreakingApartStats() -> void;

#endif //BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_BREAKINGAPART_H
