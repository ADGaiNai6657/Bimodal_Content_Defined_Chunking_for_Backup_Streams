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

#include "BreakingApart.h"
#include "DataAndMethod.h"

// Windows 控制台默认使用本地码页（简体中文为 GBK/936），
// 而程序字符串是 UTF-8；把控制台码页切到 UTF-8 可避免中文乱码。
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

/*TTTD 算法核心迁移自 TTTD_Experiments*/

using ull = unsigned long long;

// 处理模式：baseline（TTTD 直接分块）或拆分式（论文 2.3）。
enum class Mode { Baseline, BreakingApart };

// 拆分式小块器相对大块器的缩小倍数（论文取 4–8）。
constexpr std::size_t kBaSmallDivisor = 4;

// 以基准 TTTD 参数为 1 倍，按 scale 放大得到大块器；小块器 = 大块器 / kBaSmallDivisor。
auto makeBreakingConfig(const std::size_t scale) -> BreakingApartConfig {
    const ChunkerParams big{
            CONST_VALUE_MAIN_D * scale,
            CONST_VALUE_SECOND_D * scale,
            MIN_T * scale,
            MAX_T * scale,
            MY_LENGTH};
    return {big, deriveSmallParams(big, kBaSmallDivisor)};
}

// 当前生效的拆分式参数（由主菜单选择大块尺寸后设置）。
BreakingApartConfig gBreakingConfig = makeBreakingConfig(1);

// 累计统计快照，用于计算「单个文件 / 单个备份」的增量。
struct RunStats {
    std::size_t totalChunks;
    std::size_t uniqueChunks;
    std::size_t dupChunks;
    std::size_t totalBytes;
    std::size_t uniqueBytes;
    std::size_t baBig;
    std::size_t baDupBig;
    std::size_t baRechunk;
    std::size_t baSmall;
};

//快照统计，仅签名，用于创建一个RunStat对象并返回
auto snapshotStats() -> RunStats {
    return {gTotalChunks,
              gChunkPool.size(),
                gDupChunks,
                 gTotalBytes,
               gUniqueBytes,
                    gBaBigChunks,
                 gBaDupBigChunks,
                          gBaRechunkRegions,
                          gBaSmallChunks};
}

// 打印单个文件的增量与累计 DER，便于按备份版本定位 2.3 的效果。
auto reportFileDelta(const std::filesystem::path& path,
                     const Mode mode,
                     const RunStats& before,
                     const RunStats& after)
                     -> void {

    //DER计算器
    const double cumDer = after.uniqueBytes
                              ? static_cast<double>(after.totalBytes) / static_cast<double>(after.uniqueBytes)
                              : 0.0;

    std::cout << "  " << path.filename().string()
              << " size=" << (after.totalBytes - before.totalBytes)
              << " chunks+=" << (after.totalChunks - before.totalChunks)
              << " unique+=" << (after.uniqueChunks - before.uniqueChunks)
              << " dup+=" << (after.dupChunks - before.dupChunks)
              << " uniqueBytes+=" << (after.uniqueBytes - before.uniqueBytes)
              << " cumDER=" << std::fixed << std::setprecision(4) << cumDer;

    if (mode == Mode::BreakingApart) {
        std::cout << " big+=" << (after.baBig - before.baBig)
                  << " dupBig+=" << (after.baDupBig - before.baDupBig)
                  << " rechunk+=" << (after.baRechunk - before.baRechunk)
                  << " small+=" << (after.baSmall - before.baSmall);
    }

    std::cout << '\n';
}

// 旧版按行读入的缓存，仅 strPushback 使用；正式流程不再走这里。
// std::vector<std::string> str;

// 读取单个文件并分块，返回后源缓冲即释放。
auto processFile(const std::filesystem::path& path, const Mode mode) -> void {
    std::ifstream ifs(path, std::ios::binary);  //使用二进制方式读取文件
    if (!ifs) {
        std::cerr << "open failed: " << path.string() << '\n';
        return;
    }
    // 每个文件只保留这一份原始字节，分块后立即释放。
    std::string buffer((std::istreambuf_iterator<char>(ifs)),
                       std::istreambuf_iterator<char>());
    if (mode == Mode::Baseline) {    //是否普通模式/BA模式？
        pFinder(buffer);
    } else {
        processFileBreakingApart(buffer, gBreakingConfig);
    }
}

// 遍历目录下所有普通文件逐个分块。
// recursive=false：只处理顶层文件（DataSet_1）。
// recursive=true ：连子目录一起遍历（DataSet_2 的解压源码树）。
auto processDirectory(const std::filesystem::path& dir,
                      const bool recursive,
                      const Mode mode)
                      -> void {

    namespace fs = std::filesystem;

    std::vector<fs::path> files;
    std::error_code ec;
    if (recursive) {
        for (fs::recursive_directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
            if (ec) {
                std::cerr << "iteration error: " << ec.message() << '\n';
                break;
            }
            if (it->is_regular_file()) {
                files.push_back(it->path());
            }
        }
    } else {
        for (fs::directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
            if (ec) {
                std::cerr << "iteration error: " << ec.message() << '\n';
                break;
            }
            if (it->is_regular_file()) {
                files.push_back(it->path());
            }
        }
    }

    // 稳定顺序：保证备份按版本先后处理（backup stream 语义）。
    std::sort(files.begin(), files.end());

    // 文件较少时（如 DataSet_1/3）逐文件报告，便于定位每个备份的效果。
    const bool perFile = files.size() <= 32;
    for (const auto& path : files) {
        if (!perFile) {
            processFile(path, mode);
            continue;
        }
        const RunStats before = snapshotStats();
        processFile(path, mode);
        reportFileDelta(path, mode, before, snapshotStats());
    }
}

// 目标既可以是单个文件，也可以是一个目录；目录按 recursive 决定是否递归。
//内部会调用切分函数
auto resolver(const std::filesystem::path& target,  //目标路径
              const bool recursive,                 //是否使用递归遍历？
              const Mode mode)                      //Baseline 还是 BA？
-> void {

    namespace fs = std::filesystem;

    std::error_code ec;
    if (fs::is_regular_file(target, ec)) {       //普通文件？普通遍历
        const RunStats before = snapshotStats();    //new一个数据结构对象出来
        processFile(target, mode);                  //开始切分
        reportFileDelta(target, mode, before, snapshotStats()); //控制台打印报告
    } else if (fs::is_directory(target, ec)) {      //文件夹目录？递归遍历
        processDirectory(target, recursive, mode);  //递归遍历文件夹并切分
    } else {
        std::cerr << "not a file or directory: " << target.string() << '\n';
        return;
    }

    vDiff = diffCalculator(vPosition);
}

// 兼容不同工作目录：优先 ../Dataset（在构建目录下运行），其次 Dataset（在仓库根运行）。
auto datasetRoot() -> std::filesystem::path {
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

// 入口：交互式选择数据集与算法。
//   1/3/5：baseline（TTTD 直接分块），数据集依次为 DataSet_1 / DataSet_3 / DataSet_4。
//   2：baseline 跑 DataSet_2（源码树，递归）。
//   4：拆分式（论文 2.3）跑 DataSet_3（未压缩 tar 备份流）。
//   6：拆分式（论文 2.3）跑 DataSet_4（合成集中变更备份流）。
// 测试时可用管道喂入选择，例如：echo 6 | baseline.exe
int main() {
#ifdef _WIN32
    // 让控制台按 UTF-8 解码程序输出：既修复中文乱码，也避免 CR 被吞导致的行重叠。
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    namespace fs = std::filesystem;

    const fs::path root = datasetRoot();
    const fs::path kDataSet1 = root / "DataSet_1";
    const fs::path kDataSet2 = root / "DataSet_2";
    const fs::path kDataSet3 = root / "DataSet_3";
    const fs::path kDataSet4 = root / "DataSet_4";

    std::cout << "请选择运行项：\n"
              << "  1) DataSet_1 —— tar.gz 压缩包（baseline TTTD）\n"
              << "  2) DataSet_2 —— 解压源码树（baseline TTTD，递归）\n"
              << "  3) DataSet_3 —— 未压缩 tar（baseline TTTD）\n"
              << "  4) DataSet_3 —— 拆分式（论文 2.3）\n"
              << "  5) DataSet_4 —— 合成备份流（baseline TTTD）\n"
              << "  6) DataSet_4 —— 拆分式（论文 2.3）\n"
              << "请输入 1/2/3/4/5/6: ";

    int choice = 0;
    if (!(std::cin >> choice)) {
        std::cerr << "输入无效。\n";
        return 1;
    }

    fs::path target;
    bool recursive = false; //是否递归遍历？
    Mode mode = Mode::Baseline;
    //用于处理目标数据集的递归遍历与否、采用Baseline还是BA
    switch (choice) {
        case 1:
            target = kDataSet1;
            recursive = false;
            break;
        case 2:
            target = kDataSet2;
            recursive = true;
            break;
        case 3:
            target = kDataSet3;
            recursive = false;
            break;
        case 4:
            target = kDataSet3;
            recursive = false;
            mode = Mode::BreakingApart;
            break;
        case 5:
            target = kDataSet4;
            recursive = false;
            break;
        case 6:
            target = kDataSet4;
            recursive = false;
            mode = Mode::BreakingApart;
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

    if (mode == Mode::BreakingApart) {
        std::cout << "选择大块平均尺寸：\n"
                  << "  1) 约 1k（baseline 同参）\n"
                  << "  2) 约 4k\n"
                  << "  3) 约 16k\n"
                  << "  4) 约 32k\n"
                  << "请输入 1-4: ";
        int scaleChoice = 1;
        if (!(std::cin >> scaleChoice)) {
            scaleChoice = 1;
        }
        std::size_t scale = 1;
        switch (scaleChoice) {
            case 2: scale = 4; break;
            case 3: scale = 16; break;
            case 4: scale = 32; break;
            default: scale = 1; break;
        }
        gBreakingConfig = makeBreakingConfig(scale);//通过用户选定的规模来计算Config
        resetBreakingApartStats();  //重置计数器
        std::cout << "大块参数 mainD=" << gBreakingConfig.big.mainD
                  << " secondD=" << gBreakingConfig.big.secondD
                  << " minT=" << gBreakingConfig.big.minT
                  << " maxT=" << gBreakingConfig.big.maxT
                  << " window=" << gBreakingConfig.big.window
                  << "（小块 /" << kBaSmallDivisor << "）\n";
    }

    std::cout << "运行项 " << choice << "：" << target.string()
              << (recursive ? "（递归）" : "（不递归）")
              << (mode == Mode::BreakingApart ? "  算法=拆分式2.3" : "  算法=baseline TTTD")
              << '\n';

    resolver(target, recursive, mode);  //切分

    //输出具体的边界信息
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

    if (mode == Mode::BreakingApart) {
        std::cout << "bigChunks=" << gBaBigChunks
                  << " dupBigChunks=" << gBaDupBigChunks
                  << " queries=" << gBaQueryCount
                  << " rechunkRegions=" << gBaRechunkRegions
                  << " smallChunks=" << gBaSmallChunks
                  << '\n';
    }
}