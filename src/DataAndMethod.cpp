//
// Created by ADGaiNai6657 on 2026/9/13.
//
#include <iostream>
#include <fstream>

#include "DataAndMethod.h"

ChunkHash getChunkHash(const std::string_view str) {
    return static_cast<ChunkHash>(getHashValue(str));
}

/** Calculate the hash value of one data window. */
auto getHashValue(const std::string_view content)-> std::uint64_t {
    return std::hash<std::string_view>{}(content);
}

/** Return the window ending at the given position. */
auto getSubString(const std::string_view str, const std::size_t end, const std::size_t length) -> std::string_view {
    if (end < length) {
        return str.substr(0, end); // Use all available bytes at the start.
    }
    return str.substr(end - length, length); // Keep a fixed-size window.
}

auto getSubString(const Chunk &chunk) -> std::string_view {
    return static_cast<std::string_view>(chunk.data);
}

/** Through the vector to calculate the diff between every beside num*/
std::vector<std::vector<ull>> diffCalculator(std::vector<std::vector<ull>> v) {
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

/** Find TTTD-S chunk boundaries for every loaded file. */
void pFinder(std::vector<std::string>& str) {
    for (auto& s : str) {
        pFinder(s); // One boundary group per element.
    }
}

/**
 * TTTD-S 核心：对单个数据流产生边界，结果存入 vPosition 的最后一组。
 * 逻辑迁移自 TTTD-S_Experiments/TTTD-S_Algorithm.cpp 的 pFinder。
 */
void pFinder(const std::string& str) {
    vPosition.emplace_back();
    std::vector<ull>& boundaries = vPosition.back();

    std::size_t main_D = CONST_VALUE_MAIN_D;     // 当前生效的主除数。
    std::size_t second_D = CONST_VALUE_SECOND_D; // 当前生效的备份除数。
    std::size_t last_P = 0;                      // 上一个已保存边界的位置。
    std::size_t backupBreak = 0;                 // 最新的备份边界候选。

    for (std::size_t p = 0; p <= str.length(); p++) {
        const std::string_view subString = getSubString(str, p, MY_LENGTH);
        const std::size_t hash = getHashValue(subString); // 对当前窗口取哈希。

        if (p - last_P < MIN_T) {
            continue; // 保持最小块长。
        }

        if (p - last_P > SWITCH_P) {
            main_D = CONST_VALUE_SECOND_D;          // 块偏大时更容易命中主规则。
            second_D = HALF_CONST_VALUE_SECOND_D;   // 备份规则也变密。
        }

        if (hash % second_D == second_D - 1) {
            backupBreak = p; // 记住最新的备份边界。
        }

        if (hash % main_D == main_D - 1) {
            boundaries.push_back(p); // 优先使用主规则边界。
            backupBreak = 0;
            last_P = p;
            main_D = CONST_VALUE_MAIN_D;
            second_D = CONST_VALUE_SECOND_D;
            continue;
        }

        if (p - last_P < MAX_T) {
            continue; // 达到最大块长前继续搜索。
        }

        if (backupBreak != 0) {
            last_P = backupBreak;              // 使用已保存的备份边界。
            boundaries.push_back(backupBreak);
            backupBreak = 0;
        } else {
            boundaries.push_back(p);           // 否则在最大块长处强制切分。
            last_P = p;
            backupBreak = 0;
        }
        main_D = CONST_VALUE_MAIN_D;
        second_D = CONST_VALUE_SECOND_D;
    }
}

void strPushback(std::vector<std::string>& str) {
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

//输入Chunk,返回一段String_view
auto getString(const Chunk& chunk) -> std::string_view {
    return chunk.data;
}