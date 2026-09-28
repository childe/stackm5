// 骰子动画的时间线 —— 纯函数，零硬件依赖，可以在电脑上单元测试。
//
// 结果在出手那一刻就定了（dice::roll），动画只是装饰：给定「最终点数 + 随机种子
// + 已过去多少毫秒」，算出这一帧该画成什么样。同样的输入必出同样的画面，
// 所以重绘多少次都不会跳，单测也能逐帧断言。
//
// 一次摇骰子分两段：
//   1. 摇晃（rattle）：手还在晃，骰子在原地乱抖、乱换面，像在骰盅里。
//      晃的时候人看不清屏幕，所以这一段只负责「有动静」，不负责好看
//   2. 出手（tumble）：手停下来以后才开始。骰子从左边滚进来、一面一面翻、
//      弹两下落定。这一段是给人看的，所以每次翻面都要慢到看得清
//
// 翻面用「两个面的立方体转动」来假装 3D：转走的面按 cos 变窄、转来的面按 sin
// 变宽，两面贴在一起，就像骰子绕竖轴往右滚了 90°。
#pragma once

#include <cstdint>

namespace dice {

// ── 出手 ────────────────────────────────────────────────────

// 每次翻面的时长，一次比一次慢（滚得越来越没劲）。最短那次也要 90ms 以上：
// 30~40fps 下至少 3 帧，才看得出是「转过去」而不是「闪一下」
constexpr uint16_t kFlipMs[] = {95, 115, 140, 170, 210, 260};
constexpr int kFlipCount = sizeof(kFlipMs) / sizeof(kFlipMs[0]);
constexpr uint32_t flipsTotalMs()
{
    uint32_t sum = 0;
    for (int i = 0; i < kFlipCount; ++i) sum += kFlipMs[i];
    return sum;
}
// 最后一次翻完到停稳：留给最后一下小弹跳
constexpr uint32_t kRestMs = 180;
// 从出手到停稳的总时长
constexpr uint32_t kTumbleMs = flipsTotalMs() + kRestMs;
// 多颗骰子依次错开出手，落地声是一串而不是整齐的一声
constexpr uint32_t kStaggerMs = 90;
// 滚进来的距离：出手时在落点左边这么远，随翻面减速滑到落点
constexpr int kTravelPx = 60;
// 出手时离地的高度，之后弹跳着衰减到 0
constexpr int kLiftPx = 18;

struct TumbleFrame {
    uint8_t from;       // 正在转走的面（1..6）
    uint8_t to;         // 正在转来的面（1..6）；翻完以后 from == to == 最终点数
    float turn;         // 这次翻面转了多少：0 = 只看得见 from，1 = 只看得见 to
    int8_t lift;        // 离地高度，像素，<= 0（屏幕 y 向下，往上是负数）
    int8_t shift;       // 相对落点的水平偏移，像素，<= 0（从左边滚进来）
    uint8_t flipIndex;  // 已完成的翻面次数，0..kFlipCount。变了就该响一声
    bool settled;       // 停稳了：画面就是最终点数，不再需要重绘
};

// elapsedMs 是这颗骰子自己的时间（已经减掉错开的出手延迟）。
// finalFace 必须是 1..6
TumbleFrame tumbleAt(uint8_t finalFace, uint32_t seed, uint32_t elapsedMs);

// 第 i 颗（从 0 数）骰子的出手延迟
uint32_t staggerFor(int dieIndex);

// count 颗骰子全部停稳要多久
uint32_t rollDurationMs(int count);

// ── 摇晃 ────────────────────────────────────────────────────

// 摇晃时多久换一次面 / 抖一下
constexpr uint32_t kRattleStepMs = 70;
constexpr int kRattleJitterPx = 5;

struct RattleFrame {
    uint8_t face;      // 1..6，每一步都和上一步不同
    int8_t dx;         // 抖动偏移，像素，|dx| <= kRattleJitterPx
    int8_t dy;
    uint32_t step;     // 第几步。变了就该响一声
};

RattleFrame rattleAt(uint32_t seed, uint32_t elapsedMs);

// 判断「手停了，该出手了」。每次采样喂一次加速度偏离 1g 的程度（g）：
// 被 shake() 叫醒以后，偏离连续 kStillMs 都小于 kStillG 就算停手，
// 那一次 update() 返回 true（只返回一次）
class ThrowGate {
   public:
    static constexpr float kStillG = 0.25f;
    static constexpr uint32_t kStillMs = 250;

    void shake(uint32_t nowMs);
    bool update(float deviationG, uint32_t nowMs);
    bool shaking() const { return _shaking; }
    void reset();

   private:
    bool _shaking = false;
    uint32_t _lastMotionMs = 0;
};

// 点数 → 3x3 格子里哪些位置有点。bit (row*3 + col)，row/col 从 0 数，
// 左上角是 bit 0。face 越界返回 0
uint16_t pipMask(uint8_t face);

}  // namespace dice
