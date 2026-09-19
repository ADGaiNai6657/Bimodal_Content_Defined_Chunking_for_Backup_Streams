//
// Amalgamation.cpp
// 论文 2.4 合成式（k-fixed amalgamation）算法的实现。
//
// 数据流（对一条流 data）：
//   findBoundaries(data, small)               // ① 先跑小块器，得到整条流的小块切点
//   while 还有 >= k_smallsPerBig 个小块:
//       前向搜索 lookahead = 0..k_smallsPerBig，找第一个重复大块（k_smallsPerBig 个连续小块）
//         命中          -> 发射前导小块 + 发射重复大块        // Fig.3 lines 3-6
//         离开重复区    -> 发射 k_smallsPerBig 个小块作为 transition  // Fig.3 lines 7-8
//       都没命中        -> 把前 k_smallsPerBig 个小块合成一个大块发射 // Fig.3 lines 9-10
//   尾块不足 k_smallsPerBig 个小块 -> 全部按小块发射
//
// 与 2.3 的区别：2.3 先大块后按需细切；2.4 先小块后合成大块。
// 大块是数据里一段连续字节（k_smallsPerBig 个相邻小块首尾相接），因此查询/发射都无需真正拼接内容。
//

#include "Amalgamation.h"

#include <string_view>
#include <utility>
#include <vector>

#include "DataAndMethod.h"

namespace {

    /**
     * 把 [firstSmall, firstSmall+numSmalls) 这 numSmalls 个小块逐个按“小块”发射。
     * smallStart[j] 是第 j 个小块的起始字节偏移，故第 j 个小块是 [smallStart[j], smallStart[j+1])。
     * transition 区与尾部残余都走这里；每发射一个就累加 gAmSmallEmitted。
     */
    auto emitSmallsAt(const std::string& data,
                      const std::vector<std::size_t>& smallStart,
                      const std::size_t firstSmall,
                      const std::size_t numSmalls)
                      -> void {
        for (std::size_t smallIndex = firstSmall; smallIndex < firstSmall + numSmalls; ++smallIndex) {
            emitChunk(data, smallStart[smallIndex], smallStart[smallIndex + 1]);
            ++gAmSmallEmitted;
        }
    }

    /**
     * 把从第 firstSmall 个小块开始的 k_smallsPerBig 个小块合成一个大块并发射。
     * 它们首尾相接，所以大块就是
     * [smallStart[firstSmall], smallStart[firstSmall + k_smallsPerBig]) 一段连续字节。
     */
    auto emitBigAt(const std::string& data,
                   const std::vector<std::size_t>& smallStart,
                   const std::size_t firstSmall,
                   const std::size_t k_smallsPerBig)
                   -> void {
        emitChunk(data, smallStart[firstSmall], smallStart[firstSmall + k_smallsPerBig]);
        ++gAmBigChunks;
    }

} // namespace

// 合成式主流程：先算小块切点，再按 k_smallsPerBig 个小块一组做“前向搜索 + 合成”。
auto processFileAmalgamation(const std::string& data, const AmalgamationConfig& config) -> void {
    // 本条流对应的位置组与出现记录组（与 baseline / 2.3 的行为对齐）。
    vPosition.emplace_back();
    vChunks.emplace_back();

    if (data.empty()) {
        return; // 空流没有可发射的块。
    }

    // ① 小块器一次扫完整条流，得到所有小块切点。
    const std::vector<std::size_t> smallCuts = findBoundaries(data, config.small);

    // 展开成“小块起点数组”：首尾各补一个哨兵 0 与 data.size()，
    // 于是共有 smallCount = smallStart.size()-1 个小块，第 j 个为 [smallStart[j], smallStart[j+1])。
    std::vector<std::size_t> smallStartBoundaries;
    smallStartBoundaries.reserve(smallCuts.size() + 2); //预分配内存，通过.end获取的迭代器仍然指向最后一个元素的下一个位置！
    smallStartBoundaries.push_back(0);    //哨兵节点
    smallStartBoundaries.insert(smallStartBoundaries.end(), smallCuts.begin(), smallCuts.end());
    smallStartBoundaries.push_back(data.size());  //依旧哨兵节点
    const std::size_t smallCount = smallStartBoundaries.size() - 1;    //实际chunk数 = 边界数（含哨兵） - 1
    gAmSmallChunks += smallCount;

    vPosition.back().assign(smallCuts.begin(), smallCuts.end()); // 报告用：记录小块切点。

    // k_smallsPerBig 至少为 1，避免除零/空大块（配置字段为论文别名 k）。
    const std::size_t k_smallsPerBig = (config.k == 0) ? 1 : config.k;

    // ② 大块重复状态缓存：-1 未知 / 0 非重复 / 1 重复，按“起始小块下标”索引。 <-  约定
    //    懒查询 + 缓存，避免同一窗口被重复查询。
    std::vector<int> dupCache(smallCount, -1);  //用于存储是否已经查询
    const std::string_view dataView{data}; // 零拷贝视图，供 lookup 使用。

    // 查询第 firstSmall 个小块开始、长度为 k_smallsPerBig 的大块是否“以前存过”（只读，不写入）。
    const auto isBigStored = [&](const std::size_t firstSmall) -> bool {
        if (firstSmall + k_smallsPerBig > smallCount) {    //smallCount为小块总数量
            return false; // 不足 k_smallsPerBig 个小块，构不成大块。
        }
        if (dupCache[firstSmall] < 0) { //未查询过？ 进行查询
            const std::string_view big =
                    dataView.substr(smallStartBoundaries[firstSmall],
                                    smallStartBoundaries[firstSmall + k_smallsPerBig] - smallStartBoundaries[firstSmall]);
            dupCache[firstSmall] = (lookup(big) != nullptr) ? 1 : 0;    //存储过则为1，即重复；否则为未重复
            ++gAmQueryCount;
        }
        // 确定为存储过才返回 true（重复），未存储过或未知都返回 false（未重复）。
        return dupCache[firstSmall] == 1;
    };

    bool prevBigWasDup = false; // 上一个大块是否为重复块（用于识别“刚离开重复区”）。
    std::size_t nextSmall = 0;  // 当前待处理的第一块小块下标。

    while (nextSmall < smallCount) {
        // 剩余不足 k_smallsPerBig 个小块：无法再合成大块，余下全部按小块发射（尾块）。
        if (smallCount - nextSmall < k_smallsPerBig) {
            emitSmallsAt(data, smallStartBoundaries, nextSmall, smallCount - nextSmall);
            break;
        }

        bool foundDup = false;

        // ③ 前向搜索（论文 Fig.3 lines 2-6）：在 buf[0..k_smallsPerBig] 中找第一个重复大块。
        //    只有 [bigStart, bigStart+k_smallsPerBig) 完整落在流内时才检查。
        //    对齐流程图：先把整个 pos=0..k 搜索完，transition 判断放在循环外。
        for (std::size_t lookahead = 0;
             lookahead <= k_smallsPerBig && nextSmall + lookahead + k_smallsPerBig <= smallCount;//for循环只会移动pos至两个bigChunk长！
             ++lookahead) {
            const std::size_t bigStart = nextSmall + lookahead;

            if (isBigStored(bigStart)) {
                // 命中重复大块：先发射它前面的 lookahead 个前导小块，再发射该大块。
                emitSmallsAt(data, smallStartBoundaries, nextSmall, lookahead);
                emitBigAt(data, smallStartBoundaries, bigStart, k_smallsPerBig);
                ++gAmDupBigChunks;
                prevBigWasDup = true;
                nextSmall = bigStart + k_smallsPerBig; // 消费掉前导小块与该大块。 即，切换游标
                foundDup = true;
                break;
            }
        }

//------------------------------------------------------------------------------

        if (foundDup) { //找到了大块？跳过这次while；由于游标已经切换，因此则会从已找到大块的最右边开始
            continue;
        }

        // ④ 搜索失败后才判断是否刚离开重复区（对齐流程图 K 分支）：
        //    是 -> 当前 k_smallsPerBig 个小块整体按小块发射（论文 Fig.3 lines 7-8）。
        if (prevBigWasDup) {    //没找到大块？检查游标前是否为大块；是？说明前为老数据，后为新数据，处于边界处，应发射小块
            emitSmallsAt(data, smallStartBoundaries, nextSmall, k_smallsPerBig);
            prevBigWasDup = false;
            nextSmall += k_smallsPerBig;
            continue;
        }

        //没找到大块，前面也不是大块？说明在新数据内部，应发射大块
        // ⑤ 大片新数据内部：把前 k_smallsPerBig 个小块合成一个大块发射（论文 Fig.3 lines 9-10）。
        emitBigAt(data, smallStartBoundaries, nextSmall, k_smallsPerBig);
        // 依正文语义这里应置 false（Fig.3 line 10 印成 true，与“Sections fresh data” 说明矛盾）。
        prevBigWasDup = false; //此处应为论文错误：如果处于 新数据->旧数据 的边界处，如果这里为true，则会导致系统在新数据内部无法发出连续的大块
        nextSmall += k_smallsPerBig;    //游标后拨
    }
}

// 清零统计量，避免多次运行之间相互污染。
auto resetAmalgamationStats() -> void {
    gAmSmallChunks = 0;
    gAmBigChunks = 0;
    gAmDupBigChunks = 0;
    gAmQueryCount = 0;
    gAmSmallEmitted = 0;
}
