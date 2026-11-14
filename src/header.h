// 公共标题行（第 0 行，y 0..15）。所有页面都调 header::draw，不再各画各的。
//
// 左边文字区 x 0..167（21 列）：title 亮色在左，info 暗色右对齐；放不下先截 title。
// 右边状态段 x 168..239：蓝牙 / 喇叭 + 音量档位 / 电池 + 两位电量，由这里统一画。
// 截断规则和图标状态的纯逻辑在 lib/hdrlayout。
//
// 设计见 docs/superpowers/specs/2026-11-14-config-menu-header-design.md 第 1 节
#pragma once

#include <M5GFX.h>

namespace header {

// bg：背景色（NOISE 报警闪屏时不是黑色）。只用缓存的电量，不读 ADC
void draw(LovyanGFX &g, const char *title, const char *info = "", uint16_t bg = TFT_BLACK,
          uint16_t titleColor = TFT_WHITE, uint16_t infoColor = TFT_DARKGREY);

// loop 每圈调一次：蓝牙图标 / 音量 / 电量任一变化返回 true，调用方据此重绘。
// 电量 5 秒读一次；广播中蓝牙图标每 500ms 变一次相
bool tick();

}  // namespace header
