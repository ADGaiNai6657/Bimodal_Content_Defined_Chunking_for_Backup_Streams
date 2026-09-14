//
// Created by ADGaiNai6657 on 2026/9/13.
//

#ifndef BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_DATA_H
#define BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_DATA_H

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using ull = unsigned long long;

inline constexpr int D = 4;
inline constexpr int myLength = 3;

//数据结构
struct Chunk {
    std::size_t start;       // 起始位置，包含
    std::size_t end;         // 结束位置，不包含
    std::size_t length;      // chunk 长度，length = end - start
    std::string hash;        // 内容哈希
};

inline std::vector<std::vector<ull>> vPosition;
inline std::vector<std::vector<ull>> vDiff;
inline std::unordered_map<ull, Chunk> umChunk;

std::string_view getSubString(std::string_view str, int end, int length);

std::size_t getHashValue(std::string_view str);

std::vector<std::vector<ull>> diffCalculator(std::vector<std::vector<ull>> v);

void pFinder(std::vector<std::string>& str);

void pFinder(std::string& str);

void strPushback(std::vector<std::string>& str);

std::size_t lengthCalculator(Chunk chunk);

#endif //BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_DATA_H
