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
// std::vector<std::string> str;

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

// 遍历目录下所有普通文件逐个分块。
// recursive=false：只处理顶层文件（DataSet_1）。
// recursive=true ：连子目录一起遍历（DataSet_2 的解压源码树）。
void processDirectory(const std::filesystem::path& dir, bool recursive) {
    namespace fs = std::filesystem;

    std::error_code ec;
    if (recursive) {
        for (fs::recursive_directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
            if (ec) {
                std::cerr << "iteration error: " << ec.message() << '\n';
                break;
            }
            if (it->is_regular_file()) {
                processFile(it->path());
            }
        }
    } else {
        for (fs::directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
            if (ec) {
                std::cerr << "iteration error: " << ec.message() << '\n';
                break;
            }
            if (it->is_regular_file()) {
                processFile(it->path());
            }
        }
    }
}

// 目标既可以是单个文件，也可以是一个目录；目录按 recursive 决定是否递归。
void resolver(const std::filesystem::path& target, const bool recursive) {
    namespace fs = std::filesystem;

    std::error_code ec;
    if (fs::is_regular_file(target, ec)) {
        processFile(target);
    } else if (fs::is_directory(target, ec)) {
        processDirectory(target, recursive);
    } else {
        std::cerr << "not a file or directory: " << target.string() << '\n';
        return;
    }

    vDiff = diffCalculator(vPosition);
}

// 兼容不同工作目录：优先 ../Dataset（在构建目录下运行），其次 Dataset（在仓库根运行）。
std::filesystem::path datasetRoot() {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_directory("../Dataset", ec)) {
        return "../Dataset";
    }
    if (fs::is_directory("Dataset", ec)) {
        return "Dataset";
    }
    return "../Dataset"; // 都不存在时返回默认值，交由后续 exists 检查报错。
}

// 入口：交互式选择数据集。
//   DataSet_1：tar.gz 压缩包，逐文件（不递归）。
//   DataSet_2：解压后的源码树，递归全部文件。
// 测试时可用管道喂入选择，例如：echo 1 | baseline.exe
int main() {
    namespace fs = std::filesystem;

    const fs::path root = datasetRoot();
    const fs::path kDataSet1 = root / "DataSet_1";
    const fs::path kDataSet2 = root / "DataSet_2";

    std::cout << "请选择数据集：\n"
              << "  1) DataSet_1 —— tar.gz 压缩包（逐文件，不递归）\n"
              << "  2) DataSet_2 —— 解压源码树（递归全部文件）\n"
              << "请输入 1 或 2: ";

    int choice = 0;
    if (!(std::cin >> choice)) {
        std::cerr << "输入无效。\n";
        return 1;
    }

    fs::path target;
    bool recursive = false;
    switch (choice) {
        case 1:
            target = kDataSet1;
            recursive = false;
            break;
        case 2:
            target = kDataSet2;
            recursive = true;
            break;
        default:
            std::cerr << "无效选择：" << choice << '\n';
            return 1;
    }

    std::error_code ec;
    if (!fs::exists(target, ec)) {
        std::cerr << "数据集路径不存在：" << target.string() << '\n';
        return 1;
    }

    std::cout << "使用数据集 " << choice << "：" << target.string()
              << (recursive ? "（递归）" : "（不递归）") << '\n';

    resolver(target, recursive);

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