//
// Chunker.cpp
// TTTD 纯分块器的实现：从 BreakingApart 中抽出的大/小块共用切点逻辑。
// 主/备份除数固定不变（不做块大时的除数切换），与 baseline pFinder 保持同一套规则。
//

#include "Chunker.h"

#include "DataAndMethod.h"
#include "Hash.h"

// 区间分块：在 [b0, b1] 上按 TTTD 规则找切点。
// 每个位置取“以 pos 结尾”的定长窗口做滑窗哈希，再分别用主/备份除数取模判断。
auto findBoundariesInRange(const std::string_view data,
                           const std::size_t b0,
                           const std::size_t b1,
                           const ChunkerParams& params)
                           -> std::vector<std::size_t> {
    std::vector<std::size_t> boundaries; // 收集本次产生的切点（升序）。
    std::size_t last_P = b0;             // 上一个已保存边界，从区间左端起算。
    std::size_t backupBreak = 0;         // 最近一次“备份规则”命中的候选切点（0 表示没有）。

    // 逐个字节位置尝试切分。pos 走到 b1 也要处理一次（覆盖区间末尾）。
    for (std::size_t pos = b0; pos <= b1; pos++) {
        // 取“以 pos 结尾”的定长窗口：窗口来自整段 data，因此即使 pos 靠近 b0，
        // 也能看到 b0 之前的字节，切点仍是内容定义的。
        const std::string_view window = getSubString(data, pos, params.window);
        const std::uint64_t hash = static_cast<std::uint64_t>(sha1WindowHash(window));

        // ① 最小块长保护：距上一个切点太近，不切。
        if (pos - last_P < params.minT) {
            continue;
        }

        // ② 备份规则：只记录候选，不立即切。主规则一直不命中时靠它兜底。
        if (hash % params.secondD == params.secondD - 1) {
            backupBreak = pos;
        }

        // ③ 主规则：命中就切。切完重置备份候选，并从 pos 开始新块。
        if (hash % params.mainD == params.mainD - 1) {
            boundaries.push_back(pos);
            backupBreak = 0;
            last_P = pos;
            continue;
        }

        // ④ 还没到最大块长：继续往后找更好的切点。
        if (pos - last_P < params.maxT) {
            continue;
        }

        // ⑤ 已到最大块长：优先用最近的备份切点，否则就地强制切分（非内容定义）。
        if (backupBreak != 0) {
            last_P = backupBreak;
            boundaries.push_back(backupBreak);
            backupBreak = 0;
        } else {
            boundaries.push_back(pos);
            last_P = pos;
            backupBreak = 0;
        }
    }
    return boundaries;
}

// 全流分块 = 区间版的一个特例（b0=0, b1=data.size()）。
auto findBoundaries(const std::string_view data, const ChunkerParams& params) -> std::vector<std::size_t> {
    return findBoundariesInRange(data, 0, data.size(), params);
}

// 大块参数整体缩小 k 倍得到小块参数：主/备份除数变小 -> 切点更密 -> 小块更短；窗口保持不变。
auto deriveSmallParams(const ChunkerParams& big, const std::size_t k) -> ChunkerParams {
    const std::size_t divisor = (k == 0) ? 1 : k; // 防御：k=0 时退化为不缩小。
    return ChunkerParams{
            big.mainD / divisor,
            big.secondD / divisor,
            big.minT / divisor,
            big.maxT / divisor,
            big.window};
}
