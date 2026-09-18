//
// Chunker.h
// TTTD（Twin Threshold Two Divisors）纯分块器：Bimodal 两大算法（2.3 拆分式、2.4 合成式）
// 共用的参数与切点计算。
//
// 职责边界：只根据“内容”算出切点位置，不写全局存储、不发射、不改统计量。
// 大块器与小块器只是同一套代码配不同 ChunkerParams（见 deriveSmallParams）。
//

#ifndef BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_CHUNKER_H
#define BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_CHUNKER_H

#include <cstddef>
#include <string_view>
#include <vector>

// TTTD 的参数集合。把原本写死在 pFinder 里的常量抽出来，使不同粒度的分块器共用同一套逻辑。
// 经验上：平均块长 ≈ minT + mainD（例如 460 + 540 ≈ 1000 字节）。
struct ChunkerParams {
    std::size_t mainD;   // 主除数：hash % mainD == mainD-1 即命中切点；越大块越少、平均块越长。
    std::size_t secondD; // 备份除数：主规则长时间不命中时的兜底候选；一般比 mainD 小（更密）。
    std::size_t minT;    // 最小块长：小于它不产生切点，避免碎块。
    std::size_t maxT;    // 最大块长：到达该长度仍未命中主规则时，用最近的备份切点或强制切分。
    std::size_t window;  // 滑动窗口字节数；必须满足 window <= minT。
};

// 纯函数分块器（区间版）：返回 [b0, b1] 内的切点位置（升序），不发射、不改全局状态。
//
// 关键约定：
//   - 只扫描 pos ∈ [b0, b1]，不遍历整条流。
//   - 窗口取自整段 data（可越过 b0 左端），因此靠近 b0 的切点仍受前面字节影响，保持“内容定义”。
//   - min/max 的起算点是 b0（即把 b0 当作块起点）。
//   - 返回的切点严格落在 (b0, b1] 内；调用方负责处理两端残余。
auto findBoundariesInRange(std::string_view data, std::size_t b0, std::size_t b1,
                           const ChunkerParams& params) -> std::vector<std::size_t>;

// 全流分块：等价于 findBoundariesInRange(data, 0, data.size(), params)。
// 2.4 合成式先用它一次扫完整条流得到所有小块切点。
auto findBoundaries(std::string_view data, const ChunkerParams& params) -> std::vector<std::size_t>;

// 由大块参数派生小块参数：各尺寸除以 k（窗口保持不变）。k 取 4–8（论文建议）。
auto deriveSmallParams(const ChunkerParams& big, std::size_t k) -> ChunkerParams;

#endif //BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_CHUNKER_H
