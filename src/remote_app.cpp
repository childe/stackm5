#include "remote_app.h"

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <remotemap.h>

#include <cstdio>
#include <cstring>

namespace {

/*
 * HID 报告描述符：两份报告。
 *   ID 1  Keyboard        —— 方向键和回车（Android 的 UI 导航吃键盘键码）
 *   ID 2  Consumer Control —— 音量/静音/电源/主页/返回（遥控器专用 usage）
 *
 * 为什么两种都要：Android TV 对「返回」有时认键盘 ESC、有时认 Consumer 的
 * AC Back，方向键则基本只认键盘。实机测下来这台小米电视的返回走 Consumer。
 */
const uint8_t kReportMap[] = {
    // ── Report ID 1: 标准启动键盘（8 字节）──
    0x05, 0x01,              // Usage Page (Generic Desktop)
    0x09, 0x06,              // Usage (Keyboard)
    0xA1, 0x01,              // Collection (Application)
    0x85, 0x01,              //   Report ID (1)
    0x05, 0x07,              //   Usage Page (Keyboard)
    0x19, 0xE0, 0x29, 0xE7,  //   Usage Min/Max: LeftCtrl..RightGUI
    0x15, 0x00, 0x25, 0x01,  //   Logical 0..1
    0x75, 0x01, 0x95, 0x08,  //   8 个 1 位
    0x81, 0x02,              //   Input: 修饰键位图
    0x95, 0x01, 0x75, 0x08,  //
    0x81, 0x03,              //   Input: 保留字节（常量）
    0x95, 0x06, 0x75, 0x08,  //   6 个字节
    0x15, 0x00, 0x25, 0x65,  //   Logical 0..101
    0x05, 0x07,              //   Usage Page (Keyboard)
    0x19, 0x00, 0x29, 0x65,  //   Usage 0..101
    0x81, 0x00,              //   Input: 键码数组
    0xC0,                    // End Collection

    // ── Report ID 2: Consumer Control（2 字节，16 位 usage）──
    0x05, 0x0C,              // Usage Page (Consumer)
    0x09, 0x01,              // Usage (Consumer Control)
    0xA1, 0x01,              // Collection (Application)
    0x85, 0x02,              //   Report ID (2)
    0x15, 0x00,              //   Logical Min (0)
    0x26, 0xFF, 0x03,        //   Logical Max (0x3FF)
    0x19, 0x00,              //   Usage Min (0)
    0x2A, 0xFF, 0x03,        //   Usage Max (0x3FF)
    0x75, 0x10, 0x95, 0x01,  //   一个 16 位值
    0x81, 0x00,              //   Input: usage 数组
    0xC0,                    // End Collection
};

// HID 键盘键码
constexpr uint8_t kHidEnter = 0x28;
constexpr uint8_t kHidRight = 0x4F;
constexpr uint8_t kHidLeft = 0x50;
constexpr uint8_t kHidDown = 0x51;
constexpr uint8_t kHidUp = 0x52;

// Consumer Control usage
constexpr uint16_t kCcPower = 0x0030;
constexpr uint16_t kCcMute = 0x00E2;
constexpr uint16_t kCcVolUp = 0x00E9;
constexpr uint16_t kCcVolDown = 0x00EA;
constexpr uint16_t kCcHome = 0x0223;
constexpr uint16_t kCcBack = 0x0224;

// 全灰度，和另外两个 app 一致（彩色在这块 1.14" IPS 上太刺眼）
constexpr uint16_t kFgBright = 0xFFFF;
constexpr uint16_t kFgBody = 0xD69A;
constexpr uint16_t kFgDim = 0x9492;
constexpr uint16_t kFgFaint = 0x738E;

constexpr int kCharW = 8;
constexpr int kLineH = 16;

NimBLEHIDDevice *gHid = nullptr;
NimBLECharacteristic *gKbd = nullptr;
NimBLECharacteristic *gCc = nullptr;
bool gStarted = false;
bool gConnected = false;
char gLast[24] = "-";
uint32_t gSent = 0;
uint32_t gFailed = 0;

class ServerCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *, NimBLEConnInfo &) override
    {
        gConnected = true;
    }
    void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override
    {
        gConnected = false;
        // 断开后立刻重新广播。这也是待机唤醒能工作的前提：电视在待机时
        // 回连正在广播的遥控器，输入事件再把它唤醒。
        NimBLEDevice::startAdvertising();
    }
};

/*
 * 必须用 notify(value, length) 这个重载，不能用无参的 notify()。
 *
 * 无参版本 connHandle 默认是 NONE，会落到 ble_gatts_chr_updated()——那是
 * 异步的：只告诉栈「值变了」，栈在真正发送时才去读特征的当前值。而 HID
 * 一次按键要连发「按下」「松开」两份，第二份会把值覆盖成松开；栈若还没发出
 * 第一份，发送时读到的就是松开，按下那份就丢了。症状是「前几次有效、之后
 * 失效」（栈空闲时发得快、积压后竞争稳定输），而且那条路径无条件返回 true，
 * 连检查返回值都发现不了。这个 bug 实机踩过。
 *
 * 显式传值的重载会立刻把数据快照进 mbuf，无竞争，且返回真实错误码。
 */
bool notifyReport(NimBLECharacteristic *chr, const uint8_t *rpt, size_t len)
{
    if (!chr) return false;
    chr->setValue(rpt, len);  // 保留：HID 主机可能主动读取报告
    const bool ok = chr->notify(rpt, len);
    if (!ok) ++gFailed;
    return ok;
}

void sendKeyboard(uint8_t keycode)
{
    uint8_t press[8] = {0};
    press[2] = keycode;
    const uint8_t release[8] = {0};

    notifyReport(gKbd, press, sizeof(press));
    notifyReport(gKbd, release, sizeof(release));
    ++gSent;
}

void sendConsumer(uint16_t usage)
{
    const uint8_t press[2] = {(uint8_t)(usage & 0xFF), (uint8_t)(usage >> 8)};
    const uint8_t release[2] = {0, 0};

    notifyReport(gCc, press, sizeof(press));
    notifyReport(gCc, release, sizeof(release));
    ++gSent;
}

// 动作 → 发哪份报告。返回 false 表示这个动作不发给电视。
bool dispatch(remotemap::Action a)
{
    switch (a) {
        case remotemap::Action::Up: sendKeyboard(kHidUp); return true;
        case remotemap::Action::Down: sendKeyboard(kHidDown); return true;
        case remotemap::Action::Left: sendKeyboard(kHidLeft); return true;
        case remotemap::Action::Right: sendKeyboard(kHidRight); return true;
        case remotemap::Action::Ok: sendKeyboard(kHidEnter); return true;

        case remotemap::Action::VolUp: sendConsumer(kCcVolUp); return true;
        case remotemap::Action::VolDown: sendConsumer(kCcVolDown); return true;
        case remotemap::Action::Mute: sendConsumer(kCcMute); return true;
        case remotemap::Action::Home: sendConsumer(kCcHome); return true;
        case remotemap::Action::Back: sendConsumer(kCcBack); return true;
        case remotemap::Action::Power: sendConsumer(kCcPower); return true;

        case remotemap::Action::ExitApp:
        case remotemap::Action::None: break;
    }
    return false;
}

}  // namespace

void remote_app::begin()
{
    if (gStarted) return;  // BLE 只初始化一次，不做 deinit（反复 init 容易出问题）
    gStarted = true;

    NimBLEDevice::init("Cardputer Remote");
    NimBLEDevice::setPower(9);                         // dBm，够穿一间客厅
    NimBLEDevice::setSecurityAuth(true, false, true);  // 绑定 + 安全连接，不要 MITM
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);  // Android 侧 Just Works

    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCb());

    gHid = new NimBLEHIDDevice(server);
    gHid->setReportMap((uint8_t *)kReportMap, sizeof(kReportMap));
    gHid->setManufacturer("M5Stack");
    gHid->setPnp(0x02, 0x05AC, 0x820A, 0x0210);
    // flags bit0 = RemoteWake（能唤醒主机），bit1 = NormallyConnectable
    // （未连接时会广播，主机可以回连）。待机唤醒要靠这两位。
    gHid->setHidInfo(0x00, 0x03);
    gHid->setBatteryLevel(100);

    gKbd = gHid->getInputReport(1);
    gCc = gHid->getInputReport(2);

    server->start();

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->setName("Cardputer Remote");
    adv->setAppearance(0x03C1);  // 961 = HID Keyboard
    adv->addServiceUUID(gHid->getHidService()->getUUID());
    adv->enableScanResponse(true);
    adv->start();
}

void remote_app::draw(LovyanGFX &g)
{
    g.setFont(&fonts::AsciiFont8x16);
    g.setTextDatum(top_left);

    g.setTextColor(gConnected ? kFgBright : kFgDim, TFT_BLACK);
    g.drawString(gConnected ? "TV REMOTE" : "PAIRING...", 0, 0);

    char line[42];
    if (gConnected) {
        std::snprintf(line, sizeof(line), "%s", gLast);
        g.setTextColor(kFgBody, TFT_BLACK);
        g.drawString(line, 0, 22);

        std::snprintf(line, sizeof(line), "sent %lu", (unsigned long)gSent);
        g.setTextColor(kFgFaint, TFT_BLACK);
        g.drawString(line, 0, 40);
        if (gFailed) {
            std::snprintf(line, sizeof(line), "notify-fail %lu", (unsigned long)gFailed);
            g.drawString(line, 0, 56);
        }

        g.setTextColor(kFgFaint, TFT_BLACK);
        g.drawString(";., / dpad   ENTER ok", 0, 84);
        g.drawString("= - vol  m mute  b back", 0, 102);
        g.drawString("h home  p power  ` exit", 0, 120);
    } else {
        g.setTextColor(kFgFaint, TFT_BLACK);
        g.drawString("Cardputer Remote", 0, 22);
        g.drawString("On TV: Settings ->", 0, 48);
        g.drawString("Remotes & Accessories", 0, 66);
        g.drawString("-> Add accessory", 0, 84);
        g.drawString("` exit", 0, 120);
    }
}

bool remote_app::handleChar(char c)
{
    const remotemap::Action a = remotemap::fromChar(c);
    if (a == remotemap::Action::ExitApp) return false;
    if (a == remotemap::Action::None) return true;

    if (gConnected && dispatch(a)) {
        std::snprintf(gLast, sizeof(gLast), "%s", remotemap::label(a));
    }
    return true;
}

bool remote_app::handleEnter()
{
    const remotemap::Action a = remotemap::fromEnter();
    if (gConnected && dispatch(a)) {
        std::snprintf(gLast, sizeof(gLast), "%s", remotemap::label(a));
    }
    return true;
}
