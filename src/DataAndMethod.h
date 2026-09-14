//
// Created by ADGaiNai6657 on 2026/9/13.
//

#ifndef BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_DATA_H
#define BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_DATA_H

#include <unordered_map>
#include <vector>

using ull = unsigned long long;

inline constexpr int D = 4;
inline constexpr int myLength = 3;

struct Chunk;

std::vector<std::vector<ull>> vPosition;
std::vector<std::vector<ull>> vDiff;
std::unordered_map<ull,Chunk> umChunk;

//数据结构
struct Chunk {
    std::size_t start;       // 起始位置，包含
    std::size_t end;         // 结束位置，不包含
    std::size_t length;      // chunk 长度，length = eng - begin
    std::string hash;        // 内容哈希
};

auto getSubString(std::string_view str, int end, int length);

auto getHashValue(std::string_view str);

auto diffCalculator(std::vector<std::vector<ull>> v);

void pFinder(std::vector<std::string>& str);

void pFinder(std::string& str);

void strPushback(std::vector<std::string>& str);

auto lengthCalculator(Chunk chunk);

#endif //BIMODAL_CONTENT_DEFINED_CHUNKING_FOR_BACKUP_STREAMS_DATA_H
