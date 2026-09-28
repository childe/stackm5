// 骰子翻滚动画的时间线 —— 纯函数，零硬件依赖，可以在电脑上单元测试。
//
// 结果在摇的那一刻就定了（dice::roll），动画只是装饰：给定「最终点数 + 随机种子
// + 已过去多少毫秒」，算出这一帧该画成什么样。同样的输入必出同样的画面，
// 所以重绘多少次都不会跳，单测也能逐帧断言。
//
// 翻滚用「两个面的立方体转动」来假装 3D：一次翻面里，转走的面按 cos 变窄、
// 转来的面按 sin 变宽，两面贴在一起就像骰子绕一条轴转了 90°。
#pragma once

#include <cstdint>

namespace dice {

// 一颗骰子从开始翻到停稳的时长
constexpr uint32_t kTumbleMs = 900;
// 多颗骰子依次错开起步，听起来是一串「嗒嗒嗒」而不是整齐的一声
constexpr uint32_t kStaggerMs = 70;
// 一次摇动里翻几次面。先快后慢（缓出），最后一次落在最终点数上
constexpr int kFlipCount = 9;

struct TumbleFrame {
    uint8_t from;      // 正在转走的面（1..6）
    uint8_t to;        // 正在转来的面（1..6）；停稳后 from == to == 最终点数
    float turn;        // 这次翻面转了多少：0 = 只看得见 from，1 = 只看得见 to
    bool vertical;     // true = 上下翻（绕水平轴），false = 左右翻
    int8_t lift;       // 弹跳高度，像素，<= 0（屏幕 y 向下，往上跳是负数）
    int8_t shift;      // 水平晃动，像素
    uint8_t flipIndex; // 已完成的翻面次数，0..kFlipCount。变了就该响一声「嗒」
    bool settled;      // 停稳了：画面就是最终点数，不再需要重绘
};

// elapsedMs 是这颗骰子自己的时间（已经减掉错开的起步延迟）。
// finalFace 必须是 1..6
TumbleFrame tumbleAt(uint8_t finalFace, uint32_t seed, uint32_t elapsedMs);

// 第 i 颗（从 0 数）骰子相对这次摇动的起步延迟
uint32_t staggerFor(int dieIndex);

// count 颗骰子全部停稳要多久
uint32_t rollDurationMs(int count);

// 点数 → 3x3 格子里哪些位置有点。bit (row*3 + col)，row/col 从 0 数，
// 左上角是 bit 0。face 越界返回 0
uint16_t pipMask(uint8_t face);

}  // namespace dice
