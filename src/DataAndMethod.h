//
// Created by ADGaiNai6657 on 2026/9/13.
//

#ifndef BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_DATA_H
#define BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_DATA_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using ull = unsigned long long;
// using ChunkHash = std::uint64_t;

/*TTTD-S 参数：迁移自 TTTD-S_Experiments/TTTD-S_Algorithm.cpp*/
inline constexpr std::size_t CONST_VALUE_MAIN_D = 540;        // 切换前使用的主除数。
inline constexpr std::size_t CONST_VALUE_SECOND_D = 270;      // 正常情况下使用的备份除数。
inline constexpr std::size_t HALF_CONST_VALUE_SECOND_D = CONST_VALUE_SECOND_D / 2; // 切换后更密的备份除数。
inline constexpr std::size_t MAX_T = 2800;                    // 块达到该大小强制切分。
inline constexpr std::size_t MIN_T = 460;                     // 该大小之前不产生边界。
inline constexpr std::size_t MY_LENGTH = 48;                  // 滑动窗口哈希的字节数。
inline constexpr std::size_t SWITCH_P = 1600;                 // 块超过该大小后切换除数。

inline std::vector<std::vector<ull>> vPosition;
inline std::vector<std::vector<ull>> vDiff;
// inline std::unordered_map<std::size_t, Chunk> umChunk;

ChunkHash getChunkHash(std::string_view data);

std::string_view getSubString(std::string_view str, std::size_t end, std::size_t length);

std::size_t getHashValue(std::string_view str);

std::vector<std::vector<ull>> diffCalculator(std::vector<std::vector<ull>> v);

void pFinder(std::vector<std::string>& str);

void pFinder(std::string& str);

void strPushback(std::vector<std::string>& str);

// std::size_t lengthCalculator(Chunk chunk);

#endif //BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_DATA_H
