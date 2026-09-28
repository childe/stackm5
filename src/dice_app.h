// 摇骰子。晃一下 Cardputer（BMI270 加速度计）或按空格就摇，1~3 颗。
//
// 结果在摇的那一刻由硬件随机数定好，之后约 1 秒的翻滚动画只是装饰 ——
// 动画的每一帧由 lib/dice 的 tumbleAt() 按时间算出来，这里只管采样、发声和画。
#pragma once

#include <M5GFX.h>

namespace dice_app {

void begin();

// 每次 loop 调一次：采样加速度计、推进动画、翻面时发「嗒」。
// 返回 true 表示画面变了、需要重绘
bool tick();

void draw(LovyanGFX &g);

// 返回 false 表示要回菜单页
bool handleKey(char c);

}  // namespace dice_app
