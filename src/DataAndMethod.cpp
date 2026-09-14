//
// Created by ADGaiNai6657 on 2026/9/13.
//
#include <iostream>
#include <fstream>

#include "DataAndMethod.h"

/** Calculate the hash value of one data window. */
auto getHashValue(std::string_view str) {
    return std::hash<std::string_view>{}(str);
}

/** Return the window ending at the given position. */
auto getSubString(std::string_view str, int end, int length) {
    if (end < length) {
        return str.substr(0, end); // Use all available bytes at the start.
    }
    return str.substr(end - length, length); // Keep a fixed-size window.
}

/** Through the vector to calculate the diff between every beside num*/
auto diffCalculator(std::vector<std::vector<ull>> v) {
    std::vector<std::vector<ull>> result;
    for (auto j = 0; j < v.size(); j++) {
        for (auto i = 0; i < v.size()-1; i++) {
            result.push_back(std::vector<std::vector<ull> >::value_type(v[j][i + 1] - v[j][i]));
        }
    }
}

/** Find BSW chunk boundaries for every loaded file. */
void pFinder(std::vector<std::string>& str) {

    for (size_t i = 0; i < std::size(str); i++) {

        if (str[i].length() >= 4) {
            for (size_t p = 4; p <= str[i].length(); p++) {
                std::string_view subString = getSubString(str[i], p, myLength);

                if (getHashValue(subString) % D == D - 1) { // Match the BSW rule.
                    vPosition[i].push_back(p); // Save one chunk boundary.
                }
            }
        } else {
            std::cout << "Too short to insert!" << std::endl;
        }
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

auto lengthCalculator(Chunk chunk) {
    chunk.length = chunk.end - chunk.start;
}