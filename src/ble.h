// 遥控器和 AGENTS 共用的 BLE 协议栈。
//
// 为什么要共用：BLE 只能初始化一次（反复 init / deinit 容易出问题），而 NimBLE 在
// server->start() 之后再加服务，要重置 GATT 表、还得等所有连接断开才生效 —— 电视连着时
// AGENTS 服务就出不来。所以谁先进来，谁触发初始化，HID 和 AGENTS 两个服务一次注册齐，
// HID 在前（HID 的 handle 和以前一样，已绑定电视缓存的 handle 不会失效）。
//
// 同时可能有两条连接：电视（HID）和 Mac（AGENTS）。写过或订阅过 AGENTS 特征的那条
// 记为 Mac，其余归遥控器。广播按「谁还在等连接」重算，见 ble.cpp 的 refreshAdvertising()。
//
// CONFIG 页的设备列表：每条连接记一个代次（gen），订阅 HID 输入报告记为 TV、
// 认领 AGENTS 记为 Mac；已绑定设备的名字和角色存进 NVS（btdev / list）。
// GATT 读和 NVS 写都在低优先级的 ble_name 任务里做，回调和 loop 都不阻塞。
#pragma once

#include <btdev.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ble {

// 幂等。第一次调用时初始化协议栈并注册两个服务
void begin();

// 遥控器 begin() 时调，之后一直为真：遥控器没连着时就广播 HID
void setRemoteActive();

// AGENTS 进入 / 退出时调。退出时断开 Mac 那条连接
void setAgentsActive(bool on);

// AGENTS 特征被写或被订阅时调（NimBLE 回调线程），把这条连接记为 Mac
void claimMac(uint16_t connHandle);

// HID 输入报告被订阅时调（NimBLE 回调线程），把这条连接记为 TV
void claimTv(uint16_t connHandle);

bool macConnected();
bool remoteConnected();

// 给标题行用的状态查询，loop 里调
bool started();       // begin() 跑过
bool advertising();   // started() 且正在广播
bool anyConnected();  // 有任一条连接

// 设备列表的变化计数：连接、断开、绑定、名字读到、角色确认、忘记、配对进出都加 1
uint32_t devicesRevision();

// 设备列表快照（已排序），loop 里调
std::vector<btdev::Entry> devices();

// 忘记一个设备：连着先断开、删绑定、删持久记录。loop 里调
void forget(btdev::Key key);

// 配对新设备：广播 HID 120 秒，不删任何已有绑定。出现新绑定、取消或超时即退出。
// 绑定表已满时返回 BondsFull、不进入（否则 NimBLE 会挤掉最老的绑定）。loop 里调
enum class PairStart { Ok, BondsFull };
PairStart startPairing();
void stopPairing();  // 未在配对时无操作
bool pairing();
int pairingRemainingSec();

// loop 每圈调：处理配对 120 秒超时。未 begin() 时直接返回
void poll();

// 发一条消息给 Mac（notify），没连着返回 false
bool notifyMac(const std::string &line);

}  // namespace ble
