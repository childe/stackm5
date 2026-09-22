// 硬件诊断页。
//
// 加这一页是为了查一个具体问题：拔掉 USB 就断电，但插着 USB 时电量显示
// 4.1~4.3V / 100%。4.3V 高于锂电池的充满电压（4.20V），充电 IC 不会把电芯
// 充到那里，所以这个读数量的多半是系统电轨而不是电芯 —— 也就是说，
// 「电量 100%」这个显示在当前状态下不能证明电芯还在电路里。
//
// 屏幕上摆出三类事实，都是设备旁的人能直接读的：
//   1. 电压 / 百分比 / 充电状态 —— 库能给出什么，以及给不出什么
//   2. I2C 内部总线上有哪些芯片 —— M5Unified 对 Cardputer-Adv 只做了
//      ADC 那一条路（Power_Class.cpp:576 是唯一一处），如果板上还有充电或
//      电量计芯片，库根本没去问它，而它的寄存器里可能写着「电池在不在」
//   3. 电压的短期极值 —— 插拔或负载变化时电轨有没有动过
#pragma once

#include <M5GFX.h>

namespace diag_app {

void begin();
void draw(LovyanGFX &g);

// 返回 false 表示要回菜单页
bool handleKey(char c);

}  // namespace diag_app
