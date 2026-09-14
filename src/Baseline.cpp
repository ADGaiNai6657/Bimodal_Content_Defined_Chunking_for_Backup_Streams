//
// Created by ADGaiNai6657 on 2026/9/13.
//
#include<iostream>
#include<string>
#include<string_view>
#include<vector>
#include<fstream>
#include<filesystem>
#include<iterator>
#include<iomanip>
#include<algorithm>

#include "DataAndMethod.h"

/*TTTD-S 算法核心迁移自 TTTD-S_Experiments*/

using ull = unsigned long long;

// 旧版按行读入的缓存，仅 strPushback 使用；正式流程不再走这里。
std::vector<std::string> str;

// 读取单个文件并按 TTTD-S 分块，返回后源缓冲即释放。
void processFile(const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        std::cerr << "open failed: " << path.string() << '\n';
        return;
    }
    // 每个文件只保留这一份原始字节，分块后立即释放。
    std::string buffer((std::istreambuf_iterator<char>(ifs)),
                       std::istreambuf_iterator<char>());
    pFinder(buffer);
}

// 目标既可以是单个文件，也可以是一个目录（逐文件流式处理）。
void resolver(const std::filesystem::path& target) {
    namespace fs = std::filesystem;

    std::error_code ec;
    if (fs::is_regular_file(target, ec)) {
        processFile(target);
    } else if (fs::is_directory(target, ec)) {
        for (const auto& entry : fs::directory_iterator(target, ec)) {
            if (entry.is_regular_file()) {
                processFile(entry.path());
            }
        }
        if (ec) {
            std::cerr << "iteration error: " << ec.message() << '\n';
        }
    } else {
        std::cerr << "not a file or directory: " << target.string() << '\n';
        return;
    }

    vDiff = diffCalculator(vPosition);
}

// 入口：默认处理 Dataset/DataSet_1，可用命令行参数改为任意文件或目录。
// 运行示例：baseline.exe D:/.../Dataset/DataSet_1/emacs-21.4a.tar.gz
int main(int argc, char** argv) {
    const std::filesystem::path target =
            (argc > 1) ? std::filesystem::path(argv[1]) : std::filesystem::path("../Dataset/DataSet_1");
    resolver(target);

    constexpr bool kPrintBoundaries = false; // 边界数量很大，默认关闭，仅调试时打开。
    if (kPrintBoundaries) {
        for (std::size_t i = 0; i < vPosition.size(); i++) {
            std::cout << "file " << i << " boundaries:";
            for (auto boundary : vPosition[i]) {
                std::cout << ' ' << boundary;
            }
            std::cout << '\n';
        }
    }

    // 汇总：dedupRatio = 输入字节 / 唯一块字节，即论文中的 DER。
    // 恒等式：uniqueChunks + duplicateChunks == totalChunks。
    std::cout << "totalChunks=" << gTotalChunks
              << " uniqueChunks=" << gChunkPool.size()
              << " duplicateChunks=" << gDupChunks
              << " totalBytes=" << gTotalBytes
              << " uniqueBytes=" << gUniqueBytes
              << " dedupRatio="
              << (gUniqueBytes ? static_cast<double>(gTotalBytes) / static_cast<double>(gUniqueBytes)
                               : 0.0)
              << '\n';
}