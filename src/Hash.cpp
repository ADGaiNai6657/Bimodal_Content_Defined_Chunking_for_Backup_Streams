//
// Created by ADGaiNai6657 on 2026/9/16.
//
// Hash.cpp
// OpenSSL EVP 实现。复用 thread_local 的 EVP_MD_CTX，避免滑窗热循环里
// 每字节重复 new/free；同时使用 EVP API 而非已弃用的 SHA1_* 低层函数，
// 保证 -Wall -Wextra 下零弃用警告。

#include "Hash.h"

#include <cstring>

#include <openssl/evp.h>

namespace {

    /**
     * 单次 SHA-1 摘要计算，是下面三个对外函数的公共原语。
     * ctx 为 thread_local：每个线程只在首次调用时分配一次，之后一直复用，
     * 因此不会随输入增长，也避免在滑窗热循环里反复 new/free。
     * data 为待摘要的字节视图；out 接收 20 字节摘要（由调用方保证足够大）。
     */
    void sha1Into(const std::string_view data, unsigned char* out) {
        thread_local EVP_MD_CTX* ctx = EVP_MD_CTX_new(); // 每线程仅分配一次，常驻复用。
        unsigned int length = 0;
        EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr);      // 选中 SHA-1 并重置上下文。
        EVP_DigestUpdate(ctx, data.data(), data.size());  // 喂入全部字节。
        EVP_DigestFinal_ex(ctx, out, &length);            // 写出 20 字节摘要。
    }

} // namespace

/**
 * 内容哈希入口：对一段字节计算完整 SHA-1 摘要。
 * 返回 20 字节数组，作为 ChunkHash 写入唯一块并登记到 gChunkIndex。
 */
Sha1Digest sha1(const std::string_view data) {
    Sha1Digest digest{};                 // 20 字节先清零，随后被摘要完整覆盖。
    sha1Into(data, digest.data());       // 复用公共原语，写入固定长度摘要。
    return digest;
}

/**
 * 窗口哈希：对 TTTD-S 的定长窗口取 SHA-1，再截断为 64 位整数。
 * 取摘要前 8 字节按大端拼接（最高位在前），保证跨平台结果一致；
 * 边界判断只要哈希分布均匀，防碰撞交给完整 160 位的 sha1()。
 */
auto sha1WindowHash(const std::string_view data) -> std::uint64_t {
    Sha1Digest digest{};
    sha1Into(data, digest.data());       // 先得到完整 160 位摘要。
    // 取前 8 字节按大端拼成 64 位；固定字节序便于跨平台复现实验。
    //强制通过自定义规则拼接，排除机器架构所带来的干扰
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value = (value << 8) | digest[i];
    }
    return value;
}

/**
 * 把 20 字节摘要压成一个桶号，供 unordered_multimap 分桶。
 * 直接 memcpy 前 8 字节（机器字节序），比逐字节混合更快；
 * 摘要本身已均匀分布，桶号只需「同 key 同值」，不要求跨平台一致。
 * 用 memcpy 而非 reinterpret_cast，避免对齐与严格别名问题。
 */
auto Sha1DigestHash::operator()(const Sha1Digest& digest) const noexcept -> std::size_t {
    std::uint64_t value = 0;
    std::memcpy(&value, digest.data(), sizeof(value)); // 安全地读前 8 字节。
    return static_cast<std::size_t>(value);
}
