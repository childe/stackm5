// 公共标题行（第 0 行，y 0..15）。所有页面都调 header::draw，不再各画各的。
//
// 左边文字区 x 0..167（21 列）：title 亮色在左，info 暗色右对齐；放不下先截 title。
// 右边状态段 x 168..239：蓝牙 / 喇叭 + 音量档位 / 电池 + 两位电量，由这里统一画。
// 截断规则和图标状态的纯逻辑在 lib/hdrlayout。
//
// 设计见 docs/superpowers/specs/2026-11-14-config-menu-header-design.md 第 1 节
#pragma once

#include <M5GFX.h>

#include "theme.h"

namespace header {

// 颜色默认取当前主题。bg：背景色（NOISE、AGENTS 闪屏时不是底色）。只用缓存的电量，不读 ADC。
// 状态段跟着文字配色走：蓝牙亮相、音量档位、电量数字用 titleColor，
// 喇叭、电池外框、蓝牙暗相用 infoColor
void draw(LovyanGFX &g, const char *title, const char *info = "", uint16_t bg = theme::current().bg,
          uint16_t titleColor = theme::current().fg, uint16_t infoColor = theme::current().dim);

// loop 每圈调一次：蓝牙图标 / 音量 / 电量任一变化返回 true，调用方据此重绘。
// 电量 5 秒读一次；广播中蓝牙图标每 500ms 变一次相
bool tick();

}  // namespace header
