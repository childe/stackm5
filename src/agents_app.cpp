#include "agents_app.h"

#include <M5Cardputer.h>
#include <agents.h>

#include <cmath>
#include <cstdio>
#include <deque>
#include <vector>

#include "ble.h"
#include "chime.h"
#include "header.h"
#include "textfit.h"
#include "theme.h"

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

// 颜色全部取自 theme::current()，CONFIG 里切换

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
constexpr int kCardTitleY = 22;
constexpr int kCardLineH = 26;
// 图标和标题第一行垂直居中对齐（efontCN_24 字高 24），标题只有一行时两边也是齐的
constexpr int kCardIconY = kCardTitleY + 12;
constexpr int kCardIconR = 11;
constexpr int kCardTitleX = 36;
constexpr int kCardLines = 3;
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
                lines.push_back(textfit::fit(g, cur + s.substr(i), maxW));
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
    const uint16_t c = theme::current().accent;
    const int a = static_cast<int>((now / kSpinMs) % 4) * 90;
    g.drawCircle(cx, cy, r, c);
    g.fillArc(cx, cy, 0, r, a, a + 180, c);
}

// 该你了：八角星（✳）
void drawAsterisk(LovyanGFX &g, int cx, int cy, int r)
{
    const uint16_t c = theme::current().warn;
    const int d = r * 4 / 5;  // 斜线端点，让八个角看起来一样长
    for (int k = 0; k <= r / 5; ++k) {  // 大号时描粗
        g.drawLine(cx - r, cy + k, cx + r, cy + k, c);
        g.drawLine(cx + k, cy - r, cx + k, cy + r, c);
        g.drawLine(cx - d + k, cy - d, cx + d + k, cy + d, c);
        g.drawLine(cx - d + k, cy + d, cx + d + k, cy - d, c);
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
        chime::play();
        gFlashing = true;
        gFlashStart = now;
        gRot.hold(now);  // 走马灯停在触发的那个上，留时间按跳转
    }
    if (gView == View::Carousel && gRot.due(now)) {
        gModel.advance();
        dirty = true;
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

// 顶栏：左 AGENTS；右 筛选（WAIT / ALL）+ 当前位置 i/n
void drawHeader(LovyanGFX &g, bool mac, const std::vector<const agents::Session *> &vis)
{
    // 标题行走公共 header::draw：右侧状态段的蓝牙图标已经表示连没连上，
    // info 只放 ASCII（标题行用 8x16 位图字体）
    const uint32_t now = millis();
    const bool flashOn = gFlashing && ((now - gFlashStart) / kFlashMs) % 2 == 0;
    char info[24];
    if (mac) {
        std::snprintf(info, sizeof(info), "%s %d/%u",
                      gModel.filter() == agents::Filter::Waiting ? "WAIT" : "ALL",
                      gModel.cursor() + 1, static_cast<unsigned>(vis.size()));
    } else {
        std::snprintf(info, sizeof(info), "no Mac");
    }
    const theme::Palette &p = theme::current();
    header::draw(g, "AGENTS", info, flashOn ? p.flash : p.bg, p.fg, p.dim);
}

// 没有可显示的 session 时正文写什么；返回 true 表示已经写了
bool drawEmpty(LovyanGFX &g, bool mac, size_t visible)
{
    if (mac && visible > 0) return false;
    g.setFont(&fonts::efontCN_14);
    g.setTextColor(theme::current().dim);
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
    const theme::Palette &p = theme::current();
    for (int r = 0; r < kRows; ++r) {
        const int i = gTop + r;
        if (i >= static_cast<int>(vis.size())) break;
        const agents::Session &s = *vis[i];
        const int y = kRowY + r * kRowH;
        if (i == gModel.cursor()) g.fillRect(0, y, kScreenW, kRowH, p.sel);

        drawState(g, s, 7, y + 7, 5, now);

        g.setFont(&fonts::AsciiFont8x16);
        g.setTextColor(p.body);
        g.drawString(s.agent == 'x' ? "X" : "C", 16, y);

        g.setFont(&fonts::efontCN_14);
        g.setTextColor(p.fg);
        g.drawString(textfit::fit(g, s.title, kMarkX - kTitleX - 2).c_str(), kTitleX, y + 1);

        if (gModel.marked(s.id)) {
            g.setFont(&fonts::AsciiFont8x16);
            g.setTextColor(p.warn);
            g.drawString("!", kMarkX, y);
        }
    }

    g.setFont(&fonts::efontCN_14);
    if (const agents::Session *s = gModel.selected()) {
        g.setTextColor(p.dim);
        g.drawString(textfit::fit(g, s->dir, kScreenW).c_str(), 0, kDirY);
    }
}

// 走马灯：一屏一个 session，只有状态符号和标题
void drawCard(LovyanGFX &g, uint32_t now)
{
    const agents::Session *s = gModel.selected();
    if (!s) return;
    drawState(g, *s, kCardIconX, kCardIconY, kCardIconR, now);

    g.setFont(&fonts::efontCN_24);
    g.setTextColor(theme::current().fg);
    const std::vector<std::string> lines = wrap(g, s->title, kScreenW - kCardTitleX, kCardLines);
    for (size_t i = 0; i < lines.size(); ++i) {
        g.drawString(lines[i].c_str(), kCardTitleX, kCardTitleY + static_cast<int>(i) * kCardLineH);
    }
}

void drawHint(LovyanGFX &g)
{
    g.setFont(&fonts::efontCN_14);
    g.setTextColor(theme::current().dim);
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
    const theme::Palette &p = theme::current();
    const bool flashOn = gFlashing && ((now - gFlashStart) / kFlashMs) % 2 == 0;
    g.fillScreen(flashOn ? p.flash : p.bg);
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
