// CONFIG 页：全局设置。
//   音量：九档，= - 在除配色行以外的任意行都能调，每按一下按新音量响一声（断电保存，见 volume.h）
//   配色：全局配色方案，光标在这一行时 = - 循环切换（断电保存，见 theme.h）
//   蓝牙：已绑定 / 已连接设备列表（名字或地址、TV / Mac 角色、绿点 = 连着），
//         DEL 两次忘记设备；「pair new device」行 ⏎ 开始配对（不删已有绑定），再按取消
//
// 进页面调 ble::begin()（幂等），但不调 setRemoteActive()：进 CONFIG 本身不广播，
// 只有配对新设备才广播。
//
// 设计见 docs/superpowers/specs/2026-11-14-config-menu-header-design.md 第 5 节
#pragma once

#include <M5GFX.h>

namespace config_app {

// 进这一页时调：ble::begin()、重建列表、光标回到第一行
void begin();

// 离开这一页时调：退出配对模式
void end();

// 每次 loop 调一次：设备列表变化（revision）或配对剩余秒数变化 → 返回 true 要求重绘
bool tick();

void draw(LovyanGFX &g);

// ';' '.' 移光标，'=' '-' 调音量（配色行上切配色），'\b'（⌫）忘记设备；返回 false = 回菜单（`），调用方随后要调 end()
bool handleChar(char c);

// ⏎：pair 行开始 / 取消配对；配对中任意行都取消
void handleEnter();

}  // namespace config_app
