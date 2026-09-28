#include "dice_anim.h"

#include <cmath>

namespace {

constexpr float kPi = 3.14159265f;

// 弹跳：落地 3 次，高度随时间线性衰减到 0
constexpr float kLiftMaxPx = 18.0f;
constexpr float kBounces = 3.0f;
// 水平晃动：幅度同样衰减，每颗骰子相位不同，免得几颗一起左右摆
constexpr float kShiftMaxPx = 6.0f;
constexpr float kShiftCycles = 2.5f;

// 整数哈希（murmur3 的收尾混合）。同一个 seed 必出同一串数，动画才可重放
uint32_t mix(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

uint32_t stepHash(uint32_t seed, int k)
{
    return mix(seed ^ (static_cast<uint32_t>(k) * 0x9e3779b9u));
}

// 第 k 个面（k = kFlipCount 是最终点数）。从终点往回推：每往前一步
// 换成一个「不一样的」面，这样每次翻面都真的换了点数，而且最后一次
// 翻面一定落在最终点数上 —— 不会出现「翻完还是同一面」的空翻
uint8_t faceAt(uint8_t finalFace, uint32_t seed, int k)
{
    uint8_t f = finalFace;
    for (int i = dice::kFlipCount - 1; i >= k; --i) {
        const uint32_t offset = 1 + stepHash(seed, i) % 5;  // 1..5，永远不为 0
        f = static_cast<uint8_t>((f - 1 + offset) % 6 + 1);
    }
    return f;
}

}  // namespace

namespace dice {

TumbleFrame tumbleAt(uint8_t finalFace, uint32_t seed, uint32_t elapsedMs)
{
    TumbleFrame fr{};

    if (elapsedMs >= kTumbleMs) {
        fr.from = finalFace;
        fr.to = finalFace;
        fr.flipIndex = kFlipCount;
        fr.settled = true;
        return fr;
    }

    const float u = static_cast<float>(elapsedMs) / static_cast<float>(kTumbleMs);  // [0,1)
    const float left = 1.0f - u;

    // 缓出（三次方）：开头翻得飞快，越来越慢，最后一下慢慢倒向最终面。
    // 算「还剩几次没翻」而不是「翻了几次」：临近结束时 left³ 只有 1e-8 量级，
    // 1 - left³ 在 float 里会直接舍入成 1，最后几毫秒就提前跳到了终点
    const float remaining = static_cast<float>(kFlipCount) * left * left * left;  // (0, N]
    const float ceilRemaining = std::ceil(remaining);
    int j = kFlipCount - static_cast<int>(ceilRemaining);
    if (j < 0) j = 0;

    fr.from = faceAt(finalFace, seed, j);
    fr.to = faceAt(finalFace, seed, j + 1);
    fr.turn = ceilRemaining - remaining;
    fr.vertical = ((stepHash(seed, j) >> 16) & 1u) != 0;
    fr.flipIndex = static_cast<uint8_t>(j);

    fr.lift = static_cast<int8_t>(
        -std::lround(kLiftMaxPx * left * std::fabs(std::sin(kPi * kBounces * u))));

    const float phase = static_cast<float>(mix(seed) & 0xffffu) / 65536.0f * 2.0f * kPi;
    fr.shift = static_cast<int8_t>(
        std::lround(kShiftMaxPx * left * std::sin(2.0f * kPi * kShiftCycles * u + phase)));

    return fr;
}

uint32_t staggerFor(int dieIndex)
{
    return dieIndex > 0 ? static_cast<uint32_t>(dieIndex) * kStaggerMs : 0;
}

uint32_t rollDurationMs(int count)
{
    return kTumbleMs + staggerFor(count - 1);
}

uint16_t pipMask(uint8_t face)
{
    // 格子编号：
    //   0 1 2
    //   3 4 5
    //   6 7 8
    static const uint16_t kMasks[7] = {
        0x000,  // 0：无效
        0x010,  // 1：中心
        0x044,  // 2：右上 + 左下
        0x054,  // 3：2 + 中心
        0x145,  // 4：四角
        0x155,  // 5：4 + 中心
        0x16d,  // 6：左右两列
    };
    return face <= 6 ? kMasks[face] : 0;
}

}  // namespace dice
