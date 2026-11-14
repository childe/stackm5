// AGENTS 页：经蓝牙显示 Mac 上 iTerm2 里 claude / codex session 的状态，
// 状态变成「该你了」时响铃闪屏，回车让 iTerm2 切到那个 session。
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

}  // namespace agents_app
