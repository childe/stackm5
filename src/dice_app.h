// 摇骰子，1~3 颗。晃 Cardputer（BMI270 加速度计）时骰子在「骰盅」里乱撞，
// 手停下来才出手：从左边滚进来、一面一面翻、弹两下落定。按空格直接出手。
//
// 结果在出手那一刻由硬件随机数定好，之后的动画只是装饰 —— 每一帧由
// lib/dice 的 tumbleAt() / rattleAt() 按时间算出来，这里只管采样、发声和画。
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
