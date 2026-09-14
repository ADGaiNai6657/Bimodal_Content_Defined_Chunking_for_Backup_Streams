//
// Created by ADGaiNai6657 on 2026/9/13.
//
#include <iostream>
#include <fstream>

#include "DataAndMethod.h"

/** Calculate the hash value of one data window. */
std::size_t getHashValue(std::string_view str) {
    return std::hash<std::string_view>{}(str);
}

/** Return the window ending at the given position. */
std::string_view getSubString(std::string_view str, int end, int length) {
    if (end < length) {
        return str.substr(0, end); // Use all available bytes at the start.
    }
    return str.substr(end - length, length); // Keep a fixed-size window.
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

/** Find BSW chunk boundaries for every loaded file. */
void pFinder(std::vector<std::string>& str) {
    for (auto& s : str) {
        pFinder(s); // One boundary group per element.
    }
}

void pFinder(std::string& str) {
    vPosition.emplace_back();
    if (str.length() >= 4) {
        for (size_t p = 4; p <= str.length(); p++) {
            std::string_view subString = getSubString(str, p, myLength);

            if (getHashValue(subString) % D == D - 1) { // Match the BSW rule.
                vPosition.back().push_back(p); // Save one chunk boundary.
            }
        }
    } else {
        std::cout << "Too short to insert!" << std::endl;
    }
}

void strPushback(std::vector<std::string>& str) {
    std::string line;

    std::ifstream ifs("../Dataset/temp.txt");
    if (!ifs.is_open()) {
        std::cerr << "404\n";
    }
    if (ifs.is_open()) {
        while (std::getline(ifs,line)) {
            str.push_back(line);
        }
    }
}

std::size_t lengthCalculator(Chunk chunk) {
    return chunk.end - chunk.start;
}