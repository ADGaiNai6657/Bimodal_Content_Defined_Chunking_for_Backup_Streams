//
// BreakingApart.cpp
// 论文 2.3 拆分式算法的实现。
//
// 数据流（对一条流 file）：
//   findBoundaries(file, big)                 // ① 先得到大块切点（只扫一遍）
//   for 每个大块 i:
//       query(i), query(i+1)                  // ② 查“这块以前存过吗”，带 1 块前瞻
//       重复        -> emitChunk(大块)         // ③a
//       邻近重复    -> findBoundariesInRange + emitSmalls  // ③b（只在这段现算小块）
//       内部新鲜    -> emitChunk(大块)         // ③c
//
// 说明：小块切点是“按需”计算的——只有判定为 change region 的区间才会跑小块器，
//      因此无变更的文件/首个备份完全不会做小块分块，避免重复扫描整条流。
//

#include "BreakingApart.h"

#include <utility>

#include "DataAndMethod.h"
#include "Hash.h"

namespace {

    // 把 [b0, b1) 按给定的“小块切点”切成若干段，逐段交给 emitChunk 发射。
    //
    // 形状示意（s1/s2/s3 是落在区间内的小块切点）：
    //   b0 ──s1──s2──s3── b1
    //    └─段1─┘└段2┘└段3┘└尾段┘
    //
    // 参数：
    //   data  : 整条流的原始字节（切片时用绝对偏移）。
    //   b0,b1 : 当前 change region 的左右端点（左闭右开）。
    //   small : 该区间内的小块切点（升序，由 findBoundariesInRange 现算得到）。
    //
    // 注意：两端不足一小块的残余也各自成段，保证 [b0, b1) 被不重不漏地覆盖。
    auto emitSmalls(const std::string& data,                 // 整条流的原始字节。
                    const std::size_t b0,                    // 区间左端（含）。
                    const std::size_t b1,                    // 区间右端（不含）。
                    const std::vector<std::size_t>& small)   // 区间内的小块切点（升序）。
                    -> void {

        // 只发射非空段，并顺手累计小块计数。
        const auto emit = [&](const std::size_t a, const std::size_t b) -> void {
            if (b > a) {
                emitChunk(data, a, b);
                ++gBaSmallChunks;
            }
        };

        std::size_t prev = b0; // 当前待切段的起点。
        for (const std::size_t s : small) {
            if (s <= b0) {
                continue; // 切点在区间左侧之外，跳过。
            }
            if (s >= b1) {
                break; // 切点已越过区间右端，后面的更靠右，直接结束。
            }
            emit(prev, s); // 发射 [prev, s) 一段。
            prev = s;      // 下一段从 s 开始。
        }
        emit(prev, b1); // 发射最后一段（到区间右端）。
    }

} // namespace

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

auto deriveSmallParams(const ChunkerParams& big, const std::size_t k) -> ChunkerParams {
    const std::size_t divisor = (k == 0) ? 1 : k; // 防御：k=0 时退化为不缩小。
    return ChunkerParams{
            big.mainD / divisor,   // 主除数变小 -> 切点更密 -> 小块更短。
            big.secondD / divisor,
            big.minT / divisor,
            big.maxT / divisor,
            big.window};           // 窗口保持不变。
}

auto processFileBreakingApart(const std::string& data, const BreakingApartConfig& config) -> void {
    // 本条流对应的位置组与出现记录组（与 baseline 的 pFinder 行为对齐）。
    vPosition.emplace_back();
    vChunks.emplace_back();

    // ① 只先算大块切点；小块切点按需再算（见下面 change region 分支）。
    const std::vector<std::size_t> big = findBoundaries(data, config.big);
    vPosition.back().assign(big.begin(), big.end()); // 报告用：记录大块切点。

    // n 个大块，第 i 块是 [rangeOf(i).first, rangeOf(i).second)。
    const std::size_t n = big.size() + 1;

    // 把“第 i 个大块”换算成绝对区间 [b0, b1)：
    //   第 0 块从 0 开始；最后一块到 data.size() 结束；中间块由相邻大块切点界定。
    const auto rangeOf = [&](const std::size_t i) -> std::pair<std::size_t, std::size_t> {
        const std::size_t b0 = (i == 0) ? 0 : big[i - 1];
        const std::size_t b1 = (i == n - 1) ? data.size() : big[i];
        return {b0, b1};
    };

    // ② 大块重复状态：-1 未知 / 0 非重复 / 1 重复。
    //    懒查询 + 缓存，使每个大块最多查询一次（论文“每大块一次查询”）。
    std::vector<int> isDup(n, -1);

    const auto query = [&](const std::size_t i) -> bool {
        if (i >= n) {
            return false; // 越界 = 没有下一块，按“非重复”处理。
        }
        if (isDup[i] < 0) { // 尚未查询过，才真正去查。
            const auto [b0, b1] = rangeOf(i);
            // 注意这里用的是 lookup（只读、不改索引），不是 emitChunk。
            isDup[i] = (lookup(std::string_view(data).substr(b0, b1 - b0)) != nullptr) ? 1 : 0;
            ++gBaQueryCount;
        }
        return isDup[i] == 1;
    };

    bool prevDup = false; // 上一个大块是否重复（用于识别“与重复相邻”）。

    for (std::size_t i = 0; i < n; i++) {
        const auto [b0, b1] = rangeOf(i);
        const bool cur = query(i);      // 当前大块是否重复。
        const bool next = query(i + 1); // 下一个大块是否重复（前瞻 1 块）。
        ++gBaBigChunks;

        if (cur) {
            // ③a 自己就是重复块：原样按大块发射（chunkStore 会去重）。
            emitChunk(data, b0, b1);
            ++gBaDupBigChunks;
            prevDup = true;
        } else if (prevDup || next) {
            // ③b change region：自己新，但挨着重复块。
            //     只在这一段区间内现算小块切点，再切成小块发射。
            const std::vector<std::size_t> small =
                    findBoundariesInRange(data, b0, b1, config.small);
            emitSmalls(data, b0, b1, small);
            ++gBaRechunkRegions;
            prevDup = false;
        } else {
            // ③c 大片新数据内部：仍按大块发射，保持块大、元数据少。
            emitChunk(data, b0, b1);
            // 依正文/Fig.2 语义，这里应置 false（论文 Fig.1 第 6 行印成 true，属笔误）。
            prevDup = false;
        }
    }
}

auto resetBreakingApartStats() -> void {
    gBaBigChunks = 0;
    gBaDupBigChunks = 0;
    gBaQueryCount = 0;
    gBaRechunkRegions = 0;
    gBaSmallChunks = 0;
}
