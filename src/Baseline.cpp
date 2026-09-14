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

/*为方便起见，这里直接复用BSW算法作为论文Baseline实现*/

using ull = unsigned long long;

std::vector<std::string> str;
// std::vector<int> vPosition;

auto resolver() {
    strPushback(str);
    for (auto it : str) {
        pFinder(it);
        diffCalculator(vPosition);
    }
}

int main() {

}