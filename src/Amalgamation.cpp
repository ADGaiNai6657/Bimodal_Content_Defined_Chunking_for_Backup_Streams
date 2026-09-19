//
// Amalgamation.cpp
// 论文 2.4 合成式（k-fixed amalgamation）算法的实现。
//
// 数据流（对一条流 data）：
//   findBoundaries(data, small)               // ① 先跑小块器，得到整条流的小块切点
//   while 还有 >= k 个小块:
//       前向搜索 pos = 0..k，找第一个重复大块（k 个连续小块）
//         命中          -> 发射前导小块 + 发射重复大块        // Fig.3 lines 3-6
//         离开重复区    -> 发射 k 个小块作为 transition        // Fig.3 lines 7-8
//       都没命中        -> 把前 k 个小块合成一个大块发射       // Fig.3 lines 9-10
//   尾块不足 k 个小块 -> 全部按小块发射
//
// 与 2.3 的区别：2.3 先大块后按需细切；2.4 先小块后合成大块。
// 大块是数据里一段连续字节（k 个相邻小块首尾相接），因此查询/发射都无需真正拼接内容。
//

#include "Amalgamation.h"

#include <string_view>
#include <utility>
#include <vector>

#include "DataAndMethod.h"

namespace {

    /**
     * 把 [a, a+count) 这 count 个小块逐个按“小块”发射。
     * pos[j] 是第 j 个小块的起始字节偏移，故第 j 个小块是 [pos[j], pos[j+1])。
     * transition 区与尾部残余都走这里；每发射一个就累加 gAmSmallEmitted。
     */
    auto emitSmallsAt(const std::string& data,
                      const std::vector<std::size_t>& pos,
                      const std::size_t a,
                      const std::size_t count)
                      -> void {
        for (std::size_t j = a; j < a + count; ++j) {
            emitChunk(data, pos[j], pos[j + 1]);
            ++gAmSmallEmitted;
        }
    }

    /**
     * 把从第 a 个小块开始的 k 个小块合成一个大块并发射。
     * 这 k 个小块在源数据里首尾相接，所以大块就是 [pos[a], pos[a+k]) 一段连续字节。
     */
    auto emitBigAt(const std::string& data,
                   const std::vector<std::size_t>& pos,
                   const std::size_t a,
                   const std::size_t k)
                   -> void {
        emitChunk(data, pos[a], pos[a + k]);
        ++gAmBigChunks;
    }

} // namespace

// 合成式主流程：先算小块切点，再按 k 个小块一组做“前向搜索 + 合成”。
auto processFileAmalgamation(const std::string& data, const AmalgamationConfig& config) -> void {
    // 本条流对应的位置组与出现记录组（与 baseline / 2.3 的行为对齐）。
    vPosition.emplace_back();
    vChunks.emplace_back();

    if (data.empty()) {
        return; // 空流没有可发射的块。
    }

    // ① 小块器一次扫完整条流，得到所有小块切点。
    const std::vector<std::size_t> cutsPos = findBoundaries(data, config.small);

    // 展开成“小块起点数组”：首尾各补一个哨兵 0 与 data.size()，
    // 于是共有 m = starts.size()-1 个小块，第 j 个为 [starts[j], starts[j+1])。
    std::vector<std::size_t> starts;
    starts.reserve(cutsPos.size() + 2); //预分配内存，通过.end获取的迭代器仍然指向最后一个元素的下一个位置！
    starts.push_back(0);    //哨兵节点
    starts.insert(starts.end(), cutsPos.begin(), cutsPos.end());
    starts.push_back(data.size());  //依旧哨兵节点
    const std::size_t m = starts.size() - 1;    //实际chunk数 = 边界数（含哨兵） - 1
    gAmSmallChunks += m;

    vPosition.back().assign(cutsPos.begin(), cutsPos.end()); // 报告用：记录小块切点。

    // k 至少为 1，避免除零/空大块。
    const std::size_t k = (config.k == 0) ? 1 : config.k;

    // ② 大块重复状态缓存：-1 未知 / 0 非重复 / 1 重复，按“起始小块下标”索引。 <-  约定
    //    懒查询 + 缓存，避免同一窗口被重复查询。
    std::vector<int> isDup(m, -1);
    const std::string_view view{data}; // 零拷贝视图，供 lookup 使用。

    // 查询第 a 个小块开始、长度为 k 的大块是否“以前存过”（只读，不写入）。
    const auto queryBig = [&](const std::size_t a) -> bool {
        if (a + k > m) {    //m为chunk的总数量
            return false; // 不足 k 个小块，构不成大块。
        }
        if (isDup[a] < 0) { //未查询过？ 进行查询
            const std::string_view big = view.substr(starts[a], starts[a + k] - starts[a]);
            isDup[a] = (lookup(big) != nullptr) ? 1 : 0;    //存储过则为1，即重复；否则为未重复
            ++gAmQueryCount;
        }
        return isDup[a] == 1;   //确定为存储过才返回true（重复），未存储过或未知都返回false（未重复）
    };

    bool isPrevDupBig = false; // 上一个大块是否为重复块（用于识别“刚离开重复区”）。
    std::size_t i = 0;         // 当前待处理的第一块小块下标。

    while (i < m) {
        // 剩余不足 k 个小块：无法再合成大块，余下全部按小块发射（尾块）。
        if (m - i < k) {
            emitSmallsAt(data, starts, i, m - i);
            break;
        }

        bool handled = false;

        // ③ 前向搜索（论文 Fig.3 lines 2-6）：在 buf[0..k] 中找第一个重复大块。
        //    只有 [a, a+k) 完整落在流内时才检查。
        for (std::size_t pos = 0; pos <= k && i + pos + k <= m; ++pos) {
            const std::size_t a = i + pos;

            if (queryBig(a)) {
                // 命中重复大块：先发射它前面的 pos 个前导小块，再发射该大块。
                emitSmallsAt(data, starts, i, pos);
                emitBigAt(data, starts, a, k);
                ++gAmDupBigChunks;
                isPrevDupBig = true;
                i = a + k; // 消费掉前导小块与该大块。
                handled = true;
                break;
            }

            if (isPrevDupBig) {
                // 离开重复区（论文 Fig.3 lines 7-8）：当前 k 个小块整体按小块发射。
                emitSmallsAt(data, starts, i, k);
                isPrevDupBig = false;
                i += k;
                handled = true;
                break;
            }
        }

        if (handled) {
            continue;
        }

        // ④ 前 k 个小块内没有重复大块，且不处于“离开重复区”：
        //    把它们合成一个大块发射（大片新数据内部，论文 Fig.3 lines 9-10）。
        emitBigAt(data, starts, i, k);
        // 依正文语义这里应置 false（Fig.3 line 10 印成 true，与“Sections fresh data” 说明矛盾）。
        isPrevDupBig = false;
        i += k;
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
