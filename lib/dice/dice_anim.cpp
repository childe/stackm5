#include "dice_anim.h"

#include <cmath>

namespace {

constexpr float kPi = 3.14159265f;

// 弹跳：从 kLiftPx 高落下，再弹两下（半个 + 一个半周期的 |cos|），
// 高度按 (1-u)² 衰减，最后一下只剩一两个像素
constexpr float kBounceCycles = 2.5f;

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

uint32_t stepHash(uint32_t seed, uint32_t k)
{
    return mix(seed ^ (k * 0x9e3779b9u));
}

// 第 k 个面（k = kFlipCount 是最终点数）。从终点往回推：每往前一步
// 换成一个「不一样的」面，这样每次翻面都真的换了点数，而且最后一次
// 翻面一定落在最终点数上 —— 不会出现「翻完还是同一面」的空翻
uint8_t faceAt(uint8_t finalFace, uint32_t seed, int k)
{
    uint8_t f = finalFace;
    for (int i = dice::kFlipCount - 1; i >= k; --i) {
        const uint32_t offset = 1 + stepHash(seed, static_cast<uint32_t>(i)) % 5;  // 1..5，永不为 0
        f = static_cast<uint8_t>((f - 1 + offset) % 6 + 1);
    }
    return f;
}

}  // namespace

namespace dice {

TumbleFrame tumbleAt(uint8_t finalFace, uint32_t seed, uint32_t elapsedMs)
{
    TumbleFrame fr{};
    fr.from = finalFace;
    fr.to = finalFace;
    fr.flipIndex = kFlipCount;

    if (elapsedMs >= kTumbleMs) {
        fr.settled = true;
        return fr;
    }

    // 在第几次翻面里、转了多少。每次翻面内部匀速转，翻面之间一次比一次慢
    uint32_t start = 0;
    for (int j = 0; j < kFlipCount; ++j) {
        const uint32_t len = kFlipMs[j];
        if (elapsedMs < start + len) {
            fr.from = faceAt(finalFace, seed, j);
            fr.to = faceAt(finalFace, seed, j + 1);
            fr.turn = static_cast<float>(elapsedMs - start) / static_cast<float>(len);
            fr.flipIndex = static_cast<uint8_t>(j);
            break;
        }
        start += len;
    }

    // 滚进来：还剩几次没翻，就还差几份距离。翻面慢下来，滑动也跟着慢下来，
    // 看起来是「一面一面滚过来」而不是「一边滑一边闪」
    const float flipsLeft =
        static_cast<float>(kFlipCount - fr.flipIndex) - (fr.flipIndex < kFlipCount ? fr.turn : 0.0f);
    fr.shift = static_cast<int8_t>(
        -std::lround(static_cast<float>(kTravelPx) * flipsLeft / static_cast<float>(kFlipCount)));

    const float u = static_cast<float>(elapsedMs) / static_cast<float>(kTumbleMs);  // [0,1)
    const float left = 1.0f - u;
    fr.lift = static_cast<int8_t>(-std::lround(static_cast<float>(kLiftPx) * left * left *
                                               std::fabs(std::cos(kPi * kBounceCycles * u))));

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

RattleFrame rattleAt(uint32_t seed, uint32_t elapsedMs)
{
    RattleFrame fr{};
    fr.step = elapsedMs / kRattleStepMs;

    const uint32_t h = stepHash(seed, fr.step);
    // 奇数步取 4..6、偶数步取 1..3：相邻两步一定不同，又不用记住上一步
    fr.face = static_cast<uint8_t>((fr.step & 1u ? 4 : 1) + h % 3);

    constexpr uint32_t kSpan = 2 * kRattleJitterPx + 1;
    fr.dx = static_cast<int8_t>(static_cast<int>((h >> 8) % kSpan) - kRattleJitterPx);
    fr.dy = static_cast<int8_t>(static_cast<int>((h >> 16) % kSpan) - kRattleJitterPx);
    return fr;
}

void ThrowGate::shake(uint32_t nowMs)
{
    _shaking = true;
    _lastMotionMs = nowMs;
}

bool ThrowGate::update(float deviationG, uint32_t nowMs)
{
    if (!_shaking) return false;

    if (deviationG >= kStillG) {
        _lastMotionMs = nowMs;
        return false;
    }
    if (static_cast<uint32_t>(nowMs - _lastMotionMs) >= kStillMs) {
        _shaking = false;
        return true;
    }
    return false;
}

void ThrowGate::reset()
{
    _shaking = false;
    _lastMotionMs = 0;
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
