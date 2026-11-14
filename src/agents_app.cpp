#include "agents_app.h"

#include <M5Cardputer.h>
#include <agents.h>

#include <cmath>
#include <cstdio>
#include <deque>
#include <vector>

#include "ble.h"
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

// 视图和筛选在进出页面之间保留（只在 RAM 里，重启恢复默认：走马灯 + 只看该你了）
enum class View { Carousel, List };
View gView = View::Carousel;
bool gPrefsInit = false;
agents::Rotation gRot;

// 走马灯卡片：大号状态符号在左，标题用 24 号字从 kCardTitleX 起最多折 3 行
constexpr int kCardIconX = 16;
constexpr int kCardIconY = 52;
constexpr int kCardIconR = 11;
constexpr int kCardTitleX = 36;
constexpr int kCardTitleY = 22;
constexpr int kCardLineH = 26;
constexpr int kCardLines = 3;
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

// 按像素宽度把 s 折成最多 maxLines 行；最后一行还放不下就截断加 "…"。按 UTF-8 字符折，不切半个字
std::vector<std::string> wrap(LovyanGFX &g, const std::string &s, int maxW, int maxLines)
{
    std::vector<std::string> lines;
    std::string cur;
    size_t i = 0;
    while (i < s.size()) {
        size_t len = 1;
        const unsigned char c = s[i];
        if (c >= 0xF0) len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;
        const std::string next = cur + s.substr(i, len);
        if (g.textWidth(next.c_str()) > maxW && !cur.empty()) {
            if (static_cast<int>(lines.size()) + 1 == maxLines) {
                lines.push_back(fit(g, cur + s.substr(i), maxW));
                return lines;
            }
            lines.push_back(cur);
            cur.clear();
            continue;
        }
        cur = next;
        i += len;
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

// 运行中：半实心圆，每 kSpinMs 转 90°
void drawSpinner(LovyanGFX &g, int cx, int cy, int r, uint32_t now)
{
    const int a = static_cast<int>((now / kSpinMs) % 4) * 90;
    g.drawCircle(cx, cy, r, kRunColor);
    g.fillArc(cx, cy, 0, r, a, a + 180, kRunColor);
}

// 该你了：八角星（✳）
void drawAsterisk(LovyanGFX &g, int cx, int cy, int r)
{
    const int d = r * 4 / 5;  // 斜线端点，让八个角看起来一样长
    for (int k = 0; k <= r / 5; ++k) {  // 大号时描粗
        g.drawLine(cx - r, cy + k, cx + r, cy + k, kWaitColor);
        g.drawLine(cx + k, cy - r, cx + k, cy + r, kWaitColor);
        g.drawLine(cx - d + k, cy - d, cx + d + k, cy + d, kWaitColor);
        g.drawLine(cx - d + k, cy + d, cx + d + k, cy - d, kWaitColor);
    }
}

void drawState(LovyanGFX &g, const agents::Session &s, int cx, int cy, int r, uint32_t now)
{
    if (s.state == agents::State::Running) {
        drawSpinner(g, cx, cy, r, now);
    } else {
        drawAsterisk(g, cx, cy, r);
    }
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
    if (!gPrefsInit) {
        gPrefsInit = true;
        gModel.setFilter(agents::Filter::Waiting);
    }
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
        gRot.hold(now);  // 走马灯停在触发的那个上，留时间按跳转
    }
    if (gView == View::Carousel && gRot.due(now)) {
        gModel.advance();
        dirty = true;
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

namespace {

// 顶栏：左 AGENTS；右 连接点 + 筛选名 + 当前位置 i/n
void drawHeader(LovyanGFX &g, bool mac, const std::vector<const agents::Session *> &vis)
{
    g.setFont(&fonts::AsciiFont8x16);
    g.setTextColor(TFT_WHITE);
    g.drawString("AGENTS", 0, 0);

    g.setFont(&fonts::efontCN_14);
    char right[32];
    if (mac) {
        std::snprintf(right, sizeof(right), "%s %d/%u",
                      gModel.filter() == agents::Filter::Waiting ? "该你了" : "全部",
                      gModel.cursor() + 1, static_cast<unsigned>(vis.size()));
    } else {
        std::snprintf(right, sizeof(right), "Mac");
    }
    const int w = g.textWidth(right);
    g.setTextColor(mac ? TFT_WHITE : TFT_DARKGREY);
    g.drawString(right, kScreenW - w, 1);
    if (mac) {
        g.fillCircle(kScreenW - w - 9, 7, 4, TFT_GREEN);
    } else {
        g.drawCircle(kScreenW - w - 9, 7, 4, TFT_DARKGREY);
    }
}

// 没有可显示的 session 时正文写什么；返回 true 表示已经写了
bool drawEmpty(LovyanGFX &g, bool mac, size_t visible)
{
    if (mac && visible > 0) return false;
    g.setFont(&fonts::efontCN_14);
    g.setTextColor(TFT_DARKGREY);
    char msg[48];
    if (!mac) {
        std::snprintf(msg, sizeof(msg), "等待 Mac 连接…");
    } else if (gModel.sessions().empty()) {
        std::snprintf(msg, sizeof(msg), "没有 agent session");
    } else {
        std::snprintf(msg, sizeof(msg), "都在跑（%u 个）", static_cast<unsigned>(gModel.sessions().size()));
    }
    g.drawString(msg, 0, kRowY + kRowH);
    return true;
}

void drawList(LovyanGFX &g, const std::vector<const agents::Session *> &vis, uint32_t now)
{
    for (int r = 0; r < kRows; ++r) {
        const int i = gTop + r;
        if (i >= static_cast<int>(vis.size())) break;
        const agents::Session &s = *vis[i];
        const int y = kRowY + r * kRowH;
        if (i == gModel.cursor()) g.fillRect(0, y, kScreenW, kRowH, kSelBg);

        drawState(g, s, 7, y + 7, 5, now);

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
        }
    }

    g.setFont(&fonts::efontCN_14);
    if (const agents::Session *s = gModel.selected()) {
        g.setTextColor(TFT_DARKGREY);
        g.drawString(fit(g, s->dir, kScreenW).c_str(), 0, kDirY);
    }
}

// 走马灯：一屏一个 session，只有状态符号和标题
void drawCard(LovyanGFX &g, uint32_t now)
{
    const agents::Session *s = gModel.selected();
    if (!s) return;
    drawState(g, *s, kCardIconX, kCardIconY, kCardIconR, now);

    g.setFont(&fonts::efontCN_24);
    g.setTextColor(TFT_WHITE);
    const std::vector<std::string> lines = wrap(g, s->title, kScreenW - kCardTitleX, kCardLines);
    for (size_t i = 0; i < lines.size(); ++i) {
        g.drawString(lines[i].c_str(), kCardTitleX, kCardTitleY + static_cast<int>(i) * kCardLineH);
    }
}

void drawHint(LovyanGFX &g)
{
    g.setFont(&fonts::efontCN_14);
    g.setTextColor(TFT_DARKGREY);
    const char *f = gModel.filter() == agents::Filter::Waiting ? "f 全部" : "f 该你了";
    char hint[64];
    if (gView == View::Carousel) {
        std::snprintf(hint, sizeof(hint), "SPC 跳转 v 列表 %s -= %us", f,
                      static_cast<unsigned>(gRot.stepMs() / 1000));
    } else {
        std::snprintf(hint, sizeof(hint), ";. 选择 SPC 跳转 v 走马灯 %s", f);
    }
    g.drawString(hint, 0, kHintY);
}

}  // namespace

void agents_app::draw(LovyanGFX &g)
{
    const uint32_t now = millis();
    if (gFlashing && ((now - gFlashStart) / kFlashMs) % 2 == 0) g.fillScreen(kFlashBg);
    g.setTextDatum(top_left);

    const bool mac = ble::macConnected();
    const std::vector<const agents::Session *> vis = gModel.visible();

    drawHeader(g, mac, vis);
    if (!drawEmpty(g, mac, vis.size())) {
        if (gView == View::Carousel) {
            drawCard(g, now);
        } else {
            drawList(g, vis, now);
        }
    }
    drawHint(g);
}

bool agents_app::handleChar(char c)
{
    switch (c) {
        case '`':
            return false;
        case ' ':
            handleEnter();
            break;
        case 'v':
            gView = gView == View::Carousel ? View::List : View::Carousel;
            break;
        case 'f':
            gModel.setFilter(gModel.filter() == agents::Filter::Waiting ? agents::Filter::All
                                                                         : agents::Filter::Waiting);
            break;
        case '-':
            gRot.faster();  // 间隔变短
            break;
        case '=':
            gRot.slower();
            break;
        case ';':
        case '.':
            gModel.moveCursor(c == ';' ? -1 : +1);
            gRot.hold(millis());  // 手动翻过就别马上被自动轮换带走
            break;
        default:
            break;
    }
    scrollToCursor();
    return true;
}

void agents_app::handleEnter()
{
    const std::string line = gModel.focusSelected();
    if (!line.empty()) ble::notifyMac(line);
}
