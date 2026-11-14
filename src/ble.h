// 遥控器和 AGENTS 共用的 BLE 协议栈。
//
// 为什么要共用：BLE 只能初始化一次（反复 init / deinit 容易出问题），而 NimBLE 在
// server->start() 之后再加服务，要重置 GATT 表、还得等所有连接断开才生效 —— 电视连着时
// AGENTS 服务就出不来。所以谁先进来，谁触发初始化，HID 和 AGENTS 两个服务一次注册齐，
// HID 在前（HID 的 handle 和以前一样，已绑定电视缓存的 handle 不会失效）。
//
// 同时可能有两条连接：电视（HID）和 Mac（AGENTS）。写过或订阅过 AGENTS 特征的那条
// 记为 Mac，其余归遥控器。广播按「谁还在等连接」重算，见 ble.cpp 的 refreshAdvertising()。
#pragma once

#include <cstdint>
#include <string>

namespace ble {

// 幂等。第一次调用时初始化协议栈并注册两个服务
void begin();

// 遥控器 begin() 时调，之后一直为真：遥控器没连着时就广播 HID
void setRemoteActive();

// AGENTS 进入 / 退出时调。退出时断开 Mac 那条连接
void setAgentsActive(bool on);

// AGENTS 特征被写或被订阅时调（NimBLE 回调线程），把这条连接记为 Mac
void claimMac(uint16_t connHandle);

bool macConnected();
bool remoteConnected();

// 给标题行用的状态查询，loop 里调
bool started();       // begin() 跑过
bool advertising();   // started() 且正在广播
bool anyConnected();  // 有任一条连接

// 发一条消息给 Mac（notify），没连着返回 false
bool notifyMac(const std::string &line);

}  // namespace ble
