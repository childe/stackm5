// RGB565 混色 —— 零硬件依赖，可以在电脑上单元测试。
//
// 主题换了底色之后，原来「黑到白的灰阶」要变成「底色到正文色之间」的过渡色，
// 骰子、噪音计、背单词的几级亮度都靠这个。
#pragma once

#include <cstdint>

namespace rgb565 {

// a 和 b 按 t 线性混合，各通道分别算、四舍五入。t 超出 0..1 截断
uint16_t mix(uint16_t a, uint16_t b, float t);

// 亮度级 0..255 → 从 a（0）到 b（255）。a = 黑、b = 白时和 color565(v, v, v) 逐位相同
uint16_t level(uint16_t a, uint16_t b, int v);

}  // namespace rgb565
