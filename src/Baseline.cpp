//
// Created by ADGaiNai6657 on 2026/9/13.
//
#include<iostream>
#include<string>
#include<string_view>
#include<vector>
#include<fstream>
#include<filesystem>
#include<iomanip>
#include<algorithm>

#include "DataAndMethod.h"

/*TTTD-S 算法核心迁移自 TTTD-S_Experiments*/

using ull = unsigned long long;

std::vector<std::string> str;
// std::vector<int> vPosition;

void resolver() {
    strPushback(str);
    for (auto& it : str) {
        pFinder(it);
    }
    vDiff = diffCalculator(vPosition);
}

int main() {
    resolver();

    for (std::size_t i = 0; i < vPosition.size(); i++) {
        std::cout << "file " << i << " boundaries:";
        for (auto boundary : vPosition[i]) {
            std::cout << ' ' << boundary;
        }
        std::cout << '\n';
    }
}