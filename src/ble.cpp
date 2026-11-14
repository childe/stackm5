#include "ble.h"

#include <NimBLEDevice.h>

#include "agents_app.h"
#include "remote_app.h"

namespace {

constexpr int kMaxConns = 4;
constexpr uint16_t kNone = BLE_HS_CONN_HANDLE_NONE;

NimBLEServer *gServer = nullptr;
bool gStarted = false;
volatile bool gRemoteActive = false;
volatile bool gAgentsActive = false;

// 连接表在 NimBLE 回调线程里改、在 loop 里读，用自旋锁护着。
// 固定数组而不是 std::set：临界区里不能 malloc
portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
uint16_t gConns[kMaxConns] = {kNone, kNone, kNone, kNone};
uint16_t gMac = kNone;

void refreshAdvertising()
{
    const bool needRemote = gRemoteActive && !ble::remoteConnected();
    const bool needAgents = gAgentsActive && !ble::macConnected();

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->stop();
    if (!needRemote && !needAgents) return;

    adv->clearData();
    adv->enableScanResponse(true);
    if (needRemote) adv->addServiceUUID(remote_app::serviceUUID());
    if (needAgents) adv->addServiceUUID(agents_app::serviceUUID());
    adv->setAppearance(0x03C1);       // 961 = HID Keyboard
    adv->setName("Cardputer Remote");  // 开了 scan response，名字落在 scan response 里
    adv->start();
}

class ServerCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *, NimBLEConnInfo &info) override
    {
        portENTER_CRITICAL(&gMux);
        for (auto &c : gConns) {
            if (c == kNone) {
                c = info.getConnHandle();
                break;
            }
        }
        portEXIT_CRITICAL(&gMux);
        remote_app::noteConnect(info);
        refreshAdvertising();  // 连上后广播会自动停，另一方还在等的话要重新开
    }
    void onDisconnect(NimBLEServer *, NimBLEConnInfo &info, int reason) override
    {
        const uint16_t h = info.getConnHandle();
        portENTER_CRITICAL(&gMux);
        const bool wasMac = (h == gMac);
        if (wasMac) gMac = kNone;
        for (auto &c : gConns) {
            if (c == h) c = kNone;
        }
        portEXIT_CRITICAL(&gMux);
        if (!wasMac) remote_app::noteDisconnect(reason);
        refreshAdvertising();
    }
};

}  // namespace

void ble::begin()
{
    if (gStarted) return;
    gStarted = true;

    NimBLEDevice::init("Cardputer Remote");
    NimBLEDevice::setPower(9);                                  // dBm，够穿一间客厅
    NimBLEDevice::setSecurityAuth(true, false, true);           // 绑定 + 安全连接，不要 MITM
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);  // Android 侧 Just Works

    gServer = NimBLEDevice::createServer();
    gServer->setCallbacks(new ServerCb());
    // 断开后要不要广播、广播哪些服务，由 refreshAdvertising() 决定
    gServer->advertiseOnDisconnect(false);

    remote_app::registerService(gServer);  // 必须在前，见 ble.h
    agents_app::registerService(gServer);
    gServer->start();
}

void ble::setRemoteActive()
{
    gRemoteActive = true;
    refreshAdvertising();
}

void ble::setAgentsActive(bool on)
{
    gAgentsActive = on;
    if (!on) {
        portENTER_CRITICAL(&gMux);
        const uint16_t mac = gMac;
        portEXIT_CRITICAL(&gMux);
        if (mac != kNone && gServer) gServer->disconnect(mac);
    }
    refreshAdvertising();
}

void ble::claimMac(uint16_t connHandle)
{
    portENTER_CRITICAL(&gMux);
    const bool changed = (gMac != connHandle);
    gMac = connHandle;
    portEXIT_CRITICAL(&gMux);
    if (changed) refreshAdvertising();  // 这条不再算遥控器，遥控器可能又要广播了
}

bool ble::macConnected()
{
    portENTER_CRITICAL(&gMux);
    const bool r = gMac != kNone;
    portEXIT_CRITICAL(&gMux);
    return r;
}

bool ble::remoteConnected()
{
    bool r = false;
    portENTER_CRITICAL(&gMux);
    for (const auto c : gConns) {
        if (c != kNone && c != gMac) r = true;
    }
    portEXIT_CRITICAL(&gMux);
    return r;
}

bool ble::notifyMac(const std::string &line)
{
    portENTER_CRITICAL(&gMux);
    const uint16_t mac = gMac;
    portEXIT_CRITICAL(&gMux);
    if (mac == kNone) return false;
    return agents_app::notify(line, mac);
}
