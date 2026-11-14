#include "agents_app.h"

#include <M5Cardputer.h>
#include <agents.h>

#include <cmath>
#include <cstdio>
#include <deque>

#include "ble.h"
#include "header.h"
#include "timbre.h"

namespace {

constexpr const char *kServiceUuid = "a7c4f3e0-5b1d-4e8a-9c2f-1d3b6e8a0001";
constexpr const char *kWriteUuid = "a7c4f3e0-5b1d-4e8a-9c2f-1d3b6e8a0002";
constexpr const char *kNotifyUuid = "a7c4f3e0-5b1d-4e8a-9c2f-1d3b6e8a0003";
constexpr size_t kMaxQueued = 64;

// 屏幕 240x135。布局：
//   y 0..15     顶栏：左 AGENTS，右 连接状态 + session 数
//   y 17..96    5 行 session，每行 16px
//   y 100       选中项的目录
//   y 119       按键提示（贴底）
constexpr int kScreenW = 240;
constexpr int kRowY = 17;
constexpr int kRowH = 16;
constexpr int kRows = 5;
constexpr int kDirY = 100;
constexpr int kHintY = 119;
constexpr int kTitleX = 28;
constexpr int kMarkX = 230;

constexpr uint32_t kSpinMs = 150;      // 转圈每格时长，也是这一页的重绘间隔
constexpr uint32_t kFlashMs = 150;     // 闪屏亮 / 灭各 150ms
constexpr uint32_t kFlashTotalMs = 900;  // 闪 3 次
// 提示音：两个音上行（E6 → A6），各 120ms。走当前音色的波表，和编辑器试听同一条路
constexpr float kChimeHz[] = {1318.5f, 1760.0f};
constexpr uint32_t kChimeNoteMs = 120;

constexpr uint16_t kRunColor = TFT_CYAN;
constexpr uint16_t kWaitColor = TFT_ORANGE;
constexpr uint16_t kSelBg = 0x10A6;  // 深蓝灰
constexpr uint16_t kFlashBg = 0x8800;  // 暗红

agents::Model gModel;
bool gWasMac = false;
int gTop = 0;  // 第一行显示的是第几个 session
uint32_t gFlashStart = 0;
bool gFlashing = false;
uint32_t gLastSpin = 0;
int gChimeNote = -1;  // 正在响第几个音，-1 = 不响
uint32_t gChimeAt = 0;  // 这个音开始的时刻

void chimeNote(int i)
{
    const timbre::Voice &v = timbre::current();
    M5Cardputer.Speaker.tone(kChimeHz[i], kChimeNoteMs, -1, true, v.wave, v.len);
}

NimBLEService *gService = nullptr;
NimBLECharacteristic *gNotifyChr = nullptr;

// 写回调在 NimBLE 线程里把消息排进来，loop 里取走。用 FreeRTOS 互斥量而不是自旋锁：
// 入队要分配内存，不能在关中断的临界区里做
SemaphoreHandle_t gQueueLock = nullptr;
std::deque<std::string> gQueue;

class WriteCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &info) override
    {
        ble::claimMac(info.getConnHandle());
        const std::string v = chr->getValue();
        xSemaphoreTake(gQueueLock, portMAX_DELAY);
        if (gQueue.size() >= kMaxQueued) gQueue.pop_front();
        gQueue.push_back(v);
        xSemaphoreGive(gQueueLock);
    }
};

class NotifyCb : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &info, uint16_t subValue) override
    {
        if (subValue != 0) ble::claimMac(info.getConnHandle());
    }
};

}  // namespace

void agents_app::registerService(NimBLEServer *server)
{
    gQueueLock = xSemaphoreCreateMutex();
    gService = server->createService(kServiceUuid);
    NimBLECharacteristic *w = gService->createCharacteristic(kWriteUuid, NIMBLE_PROPERTY::WRITE);
    w->setCallbacks(new WriteCb());
    gNotifyChr = gService->createCharacteristic(kNotifyUuid, NIMBLE_PROPERTY::NOTIFY);
    gNotifyChr->setCallbacks(new NotifyCb());
    gService->start();
}

NimBLEUUID agents_app::serviceUUID()
{
    return NimBLEUUID(kServiceUuid);
}

bool agents_app::notify(const std::string &line, uint16_t connHandle)
{
    return gNotifyChr->notify(reinterpret_cast<const uint8_t *>(line.data()), line.size(), connHandle);
}

namespace {

// 把 s 截到画出来不超过 maxW 像素，截掉的话末尾加 "…"。按 UTF-8 字符截，不切半个字
std::string fit(LovyanGFX &g, const std::string &s, int maxW)
{
    if (g.textWidth(s.c_str()) <= maxW) return s;
    const int ellW = g.textWidth("…");
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        size_t len = 1;
        const unsigned char c = s[i];
        if (c >= 0xF0) len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;
        const std::string next = out + s.substr(i, len);
        if (g.textWidth(next.c_str()) + ellW > maxW) break;
        out = next;
        i += len;
    }
    return out + "…";
}

// 运行中：半实心圆，每 kSpinMs 转 90°
void drawSpinner(LovyanGFX &g, int cx, int cy, uint32_t now)
{
    const int a = static_cast<int>((now / kSpinMs) % 4) * 90;
    g.drawCircle(cx, cy, 5, kRunColor);
    g.fillArc(cx, cy, 0, 5, a, a + 180, kRunColor);
}

// 该你了：八角星（✳）
void drawAsterisk(LovyanGFX &g, int cx, int cy)
{
    g.drawLine(cx - 5, cy, cx + 5, cy, kWaitColor);
    g.drawLine(cx, cy - 5, cx, cy + 5, kWaitColor);
    g.drawLine(cx - 4, cy - 4, cx + 4, cy + 4, kWaitColor);
    g.drawLine(cx - 4, cy + 4, cx + 4, cy - 4, kWaitColor);
    g.drawLine(cx - 5, cy + 1, cx + 5, cy + 1, kWaitColor);
    g.drawLine(cx + 1, cy - 5, cx + 1, cy + 5, kWaitColor);
}

// 让光标所在行落在可见范围里
void scrollToCursor()
{
    const int c = gModel.cursor();
    if (c < 0) {
        gTop = 0;
        return;
    }
    if (c < gTop) gTop = c;
    if (c >= gTop + kRows) gTop = c - kRows + 1;
}

}  // namespace

void agents_app::begin()
{
    gModel.reset();
    gWasMac = false;
    gTop = 0;
    gFlashing = false;
    ble::begin();
    ble::setAgentsActive(true);
}

void agents_app::end()
{
    ble::setAgentsActive(false);
    gModel.reset();
    gFlashing = false;
}

bool agents_app::tick()
{
    bool dirty = false;
    const uint32_t now = millis();

    std::deque<std::string> lines;
    xSemaphoreTake(gQueueLock, portMAX_DELAY);
    lines.swap(gQueue);
    xSemaphoreGive(gQueueLock);

    bool alert = false;
    for (const auto &l : lines) {
        if (gModel.feedLine(l)) {
            dirty = true;
            if (!gModel.lastAlerts().empty()) alert = true;
        }
    }
    if (alert) {
        // 同一份列表里多个 session 同时触发，只响一次
        gChimeNote = 0;
        gChimeAt = now;
        chimeNote(0);
        gFlashing = true;
        gFlashStart = now;
    }
    if (gChimeNote >= 0 && now - gChimeAt >= kChimeNoteMs) {
        if (++gChimeNote < 2) {
            gChimeAt = now;
            chimeNote(gChimeNote);
        } else {
            gChimeNote = -1;
        }
    }
    if (dirty) scrollToCursor();

    const bool mac = ble::macConnected();
    if (mac != gWasMac) {
        if (!mac) {
            gModel.reset();
            gTop = 0;
        }
        gWasMac = mac;
        dirty = true;
    }

    if (gFlashing && now - gFlashStart >= kFlashTotalMs) {
        gFlashing = false;
        dirty = true;
    }
    if (now - gLastSpin >= kSpinMs) {
        gLastSpin = now;
        dirty = true;
    }
    return dirty;
}

void agents_app::draw(LovyanGFX &g)
{
    const uint32_t now = millis();
    const bool flashOn = gFlashing && ((now - gFlashStart) / kFlashMs) % 2 == 0;
    if (flashOn) g.fillScreen(kFlashBg);

    const bool mac = ble::macConnected();
    const auto &list = gModel.sessions();

    char info[16];
    if (mac) {
        std::snprintf(info, sizeof(info), "Mac %u", static_cast<unsigned>(list.size()));
    } else {
        std::snprintf(info, sizeof(info), "no Mac");
    }
    header::draw(g, "AGENTS", info, flashOn ? kFlashBg : TFT_BLACK);

    g.setFont(&fonts::efontCN_14);
    if (!mac || list.empty()) {
        g.setTextColor(TFT_DARKGREY);
        g.drawString(mac ? "没有 agent session" : "等待 Mac 连接…", 0, kRowY + kRowH);
    }

    for (int r = 0; r < kRows; ++r) {
        const int i = gTop + r;
        if (i >= static_cast<int>(list.size())) break;
        const agents::Session &s = list[i];
        const int y = kRowY + r * kRowH;
        if (i == gModel.cursor()) g.fillRect(0, y, kScreenW, kRowH, kSelBg);

        if (s.state == agents::State::Running) {
            drawSpinner(g, 7, y + 7, now);
        } else {
            drawAsterisk(g, 7, y + 7);
        }

        g.setFont(&fonts::AsciiFont8x16);
        g.setTextColor(TFT_LIGHTGREY);
        g.drawString(s.agent == 'x' ? "X" : "C", 16, y);

        g.setFont(&fonts::efontCN_14);
        g.setTextColor(TFT_WHITE);
        g.drawString(fit(g, s.title, kMarkX - kTitleX - 2).c_str(), kTitleX, y + 1);

        if (gModel.marked(s.id)) {
            g.setFont(&fonts::AsciiFont8x16);
            g.setTextColor(kWaitColor);
            g.drawString("!", kMarkX, y);
            g.setFont(&fonts::efontCN_14);
        }
    }

    g.setFont(&fonts::efontCN_14);
    if (const agents::Session *s = gModel.selected()) {
        g.setTextColor(TFT_DARKGREY);
        g.drawString(fit(g, s->dir, kScreenW).c_str(), 0, kDirY);
    }
    g.setTextColor(TFT_DARKGREY);
    g.drawString(";. 选择  ENT 跳转  ` 返回", 0, kHintY);
}

bool agents_app::handleChar(char c)
{
    if (c == '`') return false;
    if (c == ';') gModel.moveCursor(-1);
    if (c == '.') gModel.moveCursor(+1);
    scrollToCursor();
    return true;
}

void agents_app::handleEnter()
{
    const std::string line = gModel.focusSelected();
    if (!line.empty()) ble::notifyMac(line);
}
