//
// Created by ADGaiNai6657 on 2026/9/13.
//
#include <iostream>
#include <fstream>
#include <utility>

#include "DataAndMethod.h"

/** 内容哈希入口：对一段字节计算完整 SHA-1 摘要。 */
auto getChunkHash(const std::string_view str) -> ChunkHash {
    return sha1(str);
}

/** Return the window ending at the given position. */
auto getSubString(const std::string_view str, const std::size_t end, const std::size_t length) -> std::string_view {
    if (end < length) {
        return str.substr(0, end); // Use all available bytes at the start.
    }
    return str.substr(end - length, length); // Keep a fixed-size window.
}

auto getSubString(const Chunk &chunk) -> std::string_view {
    return static_cast<std::string_view>(chunk.data); // 零拷贝视图，供哈希与比较使用。
}

/** Through the vector to calculate the diff between every beside num*/
auto diffCalculator(const std::vector<std::vector<ull>>& v) -> std::vector<std::vector<ull>> {
    std::vector<std::vector<ull>> result;
    result.reserve(v.size());
    for (const auto& group : v) {
        std::vector<ull> diffs;
        for (std::size_t i = 0; i + 1 < group.size(); i++) {
            diffs.push_back(group[i + 1] - group[i]);
        }
        result.push_back(std::move(diffs));
    }
    return result;
}

// 把 [begin, end) 切片交给 ChunkStore 去重，并记录一条出现记录。
//每一次发射都将调用chunkStore()函数
auto emitChunk(const std::string& data, std::size_t begin, std::size_t end) -> void {
    if (end <= begin) {
        return; // 跳过 0 长尾块。
    }
    Chunk chunk{ChunkHash{}, data.substr(begin, end - begin), end - begin};
    Chunk* stored = chunkStore(std::move(chunk));
    vChunks.back().push_back(ChunkRef{stored, begin});
}

/** Find TTTD chunk boundaries for every loaded file. */
auto pFinder(std::vector<std::string>& str) -> void {
    for (auto& s : str) {
        pFinder(s); // One boundary group per element.
    }
}

/**
 * baseline TTTD：用基准参数 kBaselineParams 对单个数据流分块，并把块发射给 ChunkStore。
 *
 * 切点计算复用公共分块器 findBoundaries（与 2.4 小块器同一份实现），本函数只负责：
 *   1) 为这条流建立 vPosition / vChunks 记录组；
 *   2) 按相邻切点逐块 emitChunk，最后补上尾部。
 *
 * 等价性说明：TTTD 的切点判定只依赖内容窗口哈希，从不查询 ChunkStore，因此
 * “先算切点、再统一发射”与旧版“边扫描边发射”得到的边界、发射顺序与统计完全一致。
 */
auto pFinder(const std::string& str) -> void {
    vPosition.emplace_back();
    vChunks.emplace_back();

    // 公共分块器返回基准参数下的全部切点（不含文件末尾哨兵）。
    const std::vector<std::size_t> boundaries = findBoundaries(str, kBaselineParams);
    vPosition.back().assign(boundaries.begin(), boundaries.end()); // 报告用：记录切点。

    std::size_t last_P = 0; // 上一个已发射块的起点。
    for (const std::size_t boundary : boundaries) {
        emitChunk(str, last_P, boundary); // 发射 [last_P, boundary) 为一整块。
        last_P = boundary;
    }
    emitChunk(str, last_P, str.size()); // 补上尾部剩余数据。
}

// 旧版冒烟测试：按行读 Dataset/temp.txt 到 str（正式流程请用 Baseline 的二进制整读）。
auto strPushback(std::vector<std::string>& str)  -> void{
    std::ifstream ifs("../Dataset/temp.txt");
    if (!ifs.is_open()) {
        std::cerr << "404\n";
    }
    if (ifs.is_open()) {
        std::string line;
        while (std::getline(ifs,line)) {
            str.push_back(line);
        }
    }
}

// 输入 Chunk，返回其内容视图（与 getSubString(const Chunk&) 等价，保留供调用方使用）。
auto getString(const Chunk& chunk) -> std::string_view {
    return chunk.data;
}