// AGENTS 页：经蓝牙显示 Mac 上 iTerm2 里 claude / codex session 的状态，
// 状态变成「该你了」时响铃闪屏，空格 / 回车让 iTerm2 切到那个 session。
// 两种视图：走马灯（一屏一个，默认每 3 秒换，-/= 调）和列表，v 切换；f 切换只看该你了 / 全部。
//
// Mac 端是 Hammerspoon + tools/agentbridge，协议见
// docs/superpowers/specs/2026-11-14-agents-app-design.md
#pragma once

#include <M5GFX.h>
#include <NimBLEDevice.h>

#include <string>

namespace agents_app {

// 由 ble::begin() 调：建 AGENTS 服务和两个特征
void registerService(NimBLEServer *server);
NimBLEUUID serviceUUID();

// 由 ble::notifyMac() 调：用 notify(value, len) 重载发给这条连接
bool notify(const std::string &line, uint16_t connHandle);

// 进这一页时调：初始化 BLE（如果还没有），开始广播 AGENTS 服务
void begin();

// 离开这一页时必须调：断开 Mac、停 AGENTS 广播
void end();

// 每次 loop 调一次：取走蓝牙收到的消息、检测 Mac 断开、推进转圈和闪屏。
// 返回 true 表示需要重绘
bool tick();

void draw(LovyanGFX &g);

// ';' '.' 上下、空格跳转、'v' 切视图、'f' 切筛选、'-' '=' 调间隔；返回 false 表示要回菜单页（调用方随后要调 end()）
bool handleChar(char c);

// 让 Mac 切到选中的 session
void handleEnter();

}  // namespace agents_app
