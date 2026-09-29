// 噪音计：用 Adv 的麦克风（ES8311 的 ADC）实时显示环境音量、峰值、最近一分钟的曲线，
// 超过报警线时整屏闪。
//
// 麦克风和喇叭不能同时用：两边共用 ES8311 的 BCK / WS 时钟脚（GPIO41 / 43）。
// 所以进这一页时关喇叭、开麦克风，离开时（end()）必须换回来，否则其他 app 全哑。
//
// 分贝没有出厂校准：显示的是 dBFS + 校准值，只当「相对大小」看。
// 校准值和报警线存在 NVS 里，断电不丢。
#pragma once

#include <M5GFX.h>

namespace noise_app {

void begin();

// 离开这一页时必须调：停麦克风、恢复喇叭
void end();

// 每次 loop 调一次：取走录好的音频块、推进计量。返回 true 表示需要重绘
bool tick();

void draw(LovyanGFX &g);

// 返回 false 表示要回菜单页（调用方随后要调 end()）
bool handleKey(char c);

}  // namespace noise_app
