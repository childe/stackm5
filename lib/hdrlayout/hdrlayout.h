// 标题行（第 0 行）的纯逻辑 —— 零硬件依赖，可以在电脑上单元测试。
//
// 文字区 x=0..167 共 21 列：title 从左起，info 右对齐到 x=167，二者至少隔 1 列。
// 截断时先截 title，info 永不让位。状态段（蓝牙 / 音量 / 电量）的画法在 src/header.cpp。
//
// 设计见 docs/superpowers/specs/2026-11-14-config-menu-header-design.md 第 1 节
#pragma once

#include <cstdint>
#include <string>

namespace hdrlayout {

constexpr int kTextCols = 21;

struct Fit {
    std::string title;
    std::string info;
};

// 按字节计列（标题行只放 ASCII）。
// 1. info 超过 cols：info 保留 cols-1 个字符 + '~'，title 不显示
// 2. 否则 title 可用 n = cols - len(info) - (info 非空 ? 1 : 0) 列；
//    放得下原样，n >= 2 截成 n-1 个字符 + '~'，n < 2 不显示
Fit fit(const std::string &title, const std::string &info, int cols = kTextCols);

// 电量两位数字：<0 → "--"，0-9 补前导空格，>=100 → "99"
std::string batteryDigits(int pct);

// 电池框内填充宽度，0..innerW
int batteryFill(int pct, int innerW);

enum class BtIcon : uint8_t { Off, Dim, Bright };

// 未启动 → Off；广播中 → 1000ms 一周期闪烁（前 500ms 亮）；有连接 → Bright；否则 Dim
BtIcon btIcon(bool started, bool advertising, bool connected, uint32_t nowMs);

}  // namespace hdrlayout
