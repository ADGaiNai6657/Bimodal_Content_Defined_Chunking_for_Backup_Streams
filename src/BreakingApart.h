//
// BreakingApart.h
// 论文 2.3「拆分式（breaking-apart）」算法。
//
// 目标：在多次备份的序列上，尽量用“大块”去重，只在重复区与新数据的交界处改用“小块”，
// 从而在相近的重复消除率（DER）下，把平均块长做大、减少元数据。
//
// 步骤（对一条数据流）：
//   1) 用【大块器】（baseline TTTD 参数）把流切成若干大块；
//   2) 对每个大块问一次“这块以前存过吗？”（精确存在性查询 lookup）；
//        a. 存过            -> 原样按大块发射（会被去重，不新增存储）；
//        b. 没存过，但它的前一块或后一块存过 -> 这里是 change region，
//                                              只对该区间用【小块器】重切后发射；
//        c. 其余（大片新数据内部）-> 仍按大块发射。
//   判断“后一块是否重复”需要 1 个大块的前瞻；查询量约为“每大块一次”。
//
// 名词：
//   大块 / big  ：用 baseline TTTD 参数切出的块。
//   小块 / small：用更密参数切出的块，只用于 change region。
//   change region / transition：重复数据与新数据的交界邻域。
//

#ifndef BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_BREAKINGAPART_H
#define BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_BREAKINGAPART_H

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// TTTD 的参数集合。把原来写死在 pFinder 里的常量抽出来，使大块器与小块器共用同一套分块逻辑。
// 经验上：平均块长 ≈ minT + mainD（例如 460 + 540 ≈ 1000 字节）。
struct ChunkerParams {
    std::size_t mainD;   // 主除数：hash % mainD == mainD-1 即命中切点；越大块越少、平均块越长。
    std::size_t secondD; // 备份除数：主规则长时间不命中时的兜底候选；一般比 mainD 小（更密）。
    std::size_t minT;    // 最小块长：小于它不产生切点，避免碎块。
    std::size_t maxT;    // 最大块长：到达该长度仍未命中主规则时，用最近的备份切点或强制切分。
    std::size_t window;  // 滑动窗口字节数；必须满足 window <= minT。
};

// 大/小块器的参数组合。小块器一般由大块器按 k 倍缩小得到（见 deriveSmallParams）。
struct BreakingApartConfig {
    ChunkerParams big;   // 大块器参数。
    ChunkerParams small; // 小块器参数（change region 用）。
};

// 纯函数分块器（区间版）：返回 [b0, b1] 内的切点位置（升序），不发射、不改全局状态。
//
// 关键约定：
//   - 只扫描 pos ∈ [b0, b1]，不遍历整条流；这是“按需细切”的基础。
//   - 窗口取自整段 data（可越过 b0 左端），因此靠近 b0 的切点仍受前面字节影响，保持“内容定义”。
//   - min/max 的起算点是 b0（即把 b0 当作块起点）。
//   - 返回的切点严格落在 (b0, b1] 内；调用方负责处理两端残余。
auto findBoundariesInRange(std::string_view data, std::size_t b0, std::size_t b1,
                           const ChunkerParams& params) -> std::vector<std::size_t>;

// 全流分块：等价于 findBoundariesInRange(data, 0, data.size(), params)。
// 大块器用它一次扫完整条流。
auto findBoundaries(std::string_view data, const ChunkerParams& params) -> std::vector<std::size_t>;

// 由大块参数派生小块参数：各尺寸除以 k（窗口保持不变）。k 取 4–8（论文建议）。
auto deriveSmallParams(const ChunkerParams& big, std::size_t k) -> ChunkerParams;

// 对一条数据流执行拆分式分块，并把块发射给 ChunkStore（去重）。
// 内部会为本条流追加一组 vPosition / vChunks 记录。
auto processFileBreakingApart(const std::string& data, const BreakingApartConfig& config) -> void;

// ---------------------------------------------------------------------------
// 统计量（供程序末尾汇总，解释算法行为）
// ---------------------------------------------------------------------------
inline std::size_t gBaBigChunks = 0;      // 一共检查了多少个大块。
inline std::size_t gBaDupBigChunks = 0;   // 其中判定为重复（以前存过）的大块数。
inline std::size_t gBaQueryCount = 0;     // 精确存在性查询次数，约等于大块数。
inline std::size_t gBaRechunkRegions = 0; // 被小块重切的 change region 个数。
inline std::size_t gBaSmallChunks = 0;    // 重切一共产生的小块数。

// 把上面的统计量清零（每次运行 2.3 之前调用）。
auto resetBreakingApartStats() -> void;

#endif //BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_BREAKINGAPART_H
