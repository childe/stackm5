// 蓝牙电视遥控器页。
//
// 以 BLE HID（HOGP）伪装成一个遥控器，电视按标准蓝牙配件配对。
// 为什么是 BLE 而不是经典蓝牙：ESP32-S3 硬件只有 BLE
// （sdkconfig 里 CONFIG_BT_BLE_ENABLED=y，没有 CONFIG_BT_CLASSIC_ENABLED），
// 而 Android TV 的遥控器本来也走 HOGP，正好对上。
//
// 状态全封在 .cpp 里，main.cpp 只需要「开始 / 画一帧 / 喂按键」。
#pragma once

#include <M5GFX.h>

namespace remote_app {

// 进这一页时调用。第一次调用才初始化 BLE（懒加载）——
// 不进这个 app 的话不付 BLE 的运行时开销。
void begin();

void draw(LovyanGFX &g);

// 返回 false 表示要回菜单页
bool handleChar(char c);
bool handleEnter();

}  // namespace remote_app
