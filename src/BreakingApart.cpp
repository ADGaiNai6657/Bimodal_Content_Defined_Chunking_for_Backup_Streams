//
// BreakingApart.cpp
// 论文 2.3 拆分式算法的实现。详见 BreakingApart.h 的说明。
//

#include "BreakingApart.h"

#include <utility>

#include "DataAndMethod.h"
#include "Hash.h"

namespace {

// 把 [b0, b1) 按已预计算的小块切点切成若干段并发射。
// small 为整文件的小块切点（升序）；两端不足一小块的残余也各自成块。
void emitSmalls(const std::string& data, const std::size_t b0, const std::size_t b1,
                const std::vector<std::size_t>& small) {
    const auto emit = [&](const std::size_t a, const std::size_t b) {
        if (b > a) {
            emitChunk(data, a, b);
            ++gBaSmallChunks;
        }
    };

    std::size_t prev = b0;
    for (const std::size_t s : small) {
        if (s <= b0) {
            continue; // 落在区域左侧之外。
        }
        if (s >= b1) {
            break; // 已越过区域右端。
        }
        emit(prev, s);
        prev = s;
    }
    emit(prev, b1); // 区域右端残余。
}

} // namespace

std::vector<std::size_t> findBoundaries(const std::string_view data, const ChunkerParams& params) {
    std::vector<std::size_t> boundaries;
    std::size_t last_P = 0;      // 上一个已保存边界。
    std::size_t backupBreak = 0; // 最新的备份边界候选。

    for (std::size_t pos = 0; pos <= data.size(); pos++) {
        const std::string_view window = getSubString(data, pos, params.window);
        const std::size_t hash = static_cast<std::size_t>(sha1WindowHash(window));

        if (pos - last_P < params.minT) {
            continue; // 保持最小块长。
        }

        if (hash % params.secondD == params.secondD - 1) {
            backupBreak = pos; // 记住最新的备份边界。
        }

        if (hash % params.mainD == params.mainD - 1) {
            boundaries.push_back(pos); // 优先使用主规则边界。
            backupBreak = 0;
            last_P = pos;
            continue;
        }

        if (pos - last_P < params.maxT) {
            continue; // 达到最大块长前继续搜索。
        }

        if (backupBreak != 0) {
            last_P = backupBreak; // 使用已保存的备份边界。
            boundaries.push_back(backupBreak);
            backupBreak = 0;
        } else {
            boundaries.push_back(pos); // 否则在最大块长处强制切分。
            last_P = pos;
            backupBreak = 0;
        }
    }
    return boundaries;
}

ChunkerParams deriveSmallParams(const ChunkerParams& big, const std::size_t k) {
    const std::size_t divisor = (k == 0) ? 1 : k;
    return ChunkerParams{
            big.mainD / divisor,
            big.secondD / divisor,
            big.minT / divisor,
            big.maxT / divisor,
            big.window};
}

void processFileBreakingApart(const std::string& data, const BreakingApartConfig& config) {
    vPosition.emplace_back();
    vChunks.emplace_back();

    // 一次预先算出大块与小块两套切点。
    const std::vector<std::size_t> big = findBoundaries(data, config.big);
    const std::vector<std::size_t> small = findBoundaries(data, config.small);
    vPosition.back().assign(big.begin(), big.end());

    const std::size_t n = big.size() + 1; // 大块数量：切点把流分成 n 段。

    const auto rangeOf = [&](const std::size_t i) -> std::pair<std::size_t, std::size_t> {
        const std::size_t b0 = (i == 0) ? 0 : big[i - 1];
        const std::size_t b1 = (i == n - 1) ? data.size() : big[i];
        return {b0, b1};
    };

    // 懒查询 + 缓存：每个大块最多查询一次，满足论文「每大块一次查询」。
    std::vector<int> dup(n, -1); // -1 未知 / 0 非重复 / 1 重复。
    const auto query = [&](const std::size_t i) -> bool {
        if (i >= n) {
            return false; // 越界视为「无下一块」。
        }
        if (dup[i] < 0) {
            const auto [b0, b1] = rangeOf(i);
            dup[i] = (lookup(std::string_view(data).substr(b0, b1 - b0)) != nullptr) ? 1 : 0;
            ++gBaQueryCount;
        }
        return dup[i] == 1;
    };

    bool prevDup = false; // 前一个大块是否为重复。
    for (std::size_t i = 0; i < n; i++) {
        const auto [b0, b1] = rangeOf(i);
        const bool cur = query(i);
        const bool next = query(i + 1);
        ++gBaBigChunks;

        if (cur) {
            emitChunk(data, b0, b1); // 重复大块：原样发射。
            ++gBaDupBigChunks;
            prevDup = true;
        } else if (prevDup || next) {
            emitSmalls(data, b0, b1, small); // change region：按小块重切。
            ++gBaRechunkRegions;
            prevDup = false;
        } else {
            emitChunk(data, b0, b1); // 内部新鲜区：仍按大块发射。
            prevDup = false; // 依正文/Fig.2 语义修正 Fig.1 第 6 行的 true。
        }
    }
}

void resetBreakingApartStats() {
    gBaBigChunks = 0;
    gBaDupBigChunks = 0;
    gBaQueryCount = 0;
    gBaRechunkRegions = 0;
    gBaSmallChunks = 0;
}
