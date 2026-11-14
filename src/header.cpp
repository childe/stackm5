#include "header.h"

#include <M5Cardputer.h>
#include <hdrlayout.h>

#include "ble.h"
#include "volume.h"

namespace {

constexpr int kScreenW = 240;
constexpr int kH = 16;
constexpr int kCharW = 8;
constexpr int kTextRight = 168;  // 文字区 x 0..167

// 状态段的像素分配（闭区间），见 spec 第 1 节表格
constexpr int kBtX = 172;       // 172..179
constexpr int kSpkX = 182;      // 182..189
constexpr int kVolX = 190;      // 190..197
constexpr int kBatX = 204;      // 外框 204..217
constexpr int kBatW = 14;
constexpr int kBatTipX = 218;   // 正极 218..219
constexpr int kBatPctX = 224;   // 224..239

constexpr uint16_t kOffColor = 0x4208;  // 比 DARKGREY 更暗的灰
constexpr uint32_t kBatPollMs = 5000;

int gBatPct = -1;
uint32_t gBatMs = 0;
bool gBatRead = false;

hdrlayout::BtIcon btState()
{
    if (!ble::started()) return hdrlayout::BtIcon::Off;
    return hdrlayout::btIcon(true, ble::advertising(), ble::anyConnected(), millis());
}

// 蓝牙符文：竖线 + 右侧两个折角，8x16 框内居中（y 2..13）
void drawBt(LovyanGFX &g, int x, uint16_t c)
{
    const int cx = x + 3;
    const int top = 2, bot = 13, mid = 7;
    g.drawFastVLine(cx, top, bot - top + 1, c);
    g.drawLine(cx, top, cx + 3, top + 3, c);
    g.drawLine(cx + 3, top + 3, cx - 3, mid + 3, c);
    g.drawLine(cx, bot, cx + 3, bot - 3, c);
    g.drawLine(cx + 3, bot - 3, cx - 3, mid - 2, c);
}

// 喇叭：小方块 + 喇叭口三角；静音时口外画叉
void drawSpeaker(LovyanGFX &g, int x, bool muted, uint16_t c)
{
    g.fillRect(x, 6, 2, 4, c);
    g.fillTriangle(x + 2, 7, x + 5, 3, x + 5, 12, c);
    g.fillTriangle(x + 2, 8, x + 5, 3, x + 5, 12, c);
    if (muted) {
        g.drawLine(x + 6, 5, x + 7, 10, TFT_RED);
        g.drawLine(x + 7, 5, x + 6, 10, TFT_RED);
    } else {
        g.drawFastVLine(x + 7, 6, 4, c);
    }
}

void drawBattery(LovyanGFX &g, int pct, uint16_t c, uint16_t bg)
{
    const int y = 4, h = 8;
    g.drawRect(kBatX, y, kBatW, h, c);
    g.fillRect(kBatTipX, y + 2, 2, h - 4, c);
    const int innerW = kBatW - 2;
    const int fill = hdrlayout::batteryFill(pct, innerW);
    g.fillRect(kBatX + 1, y + 1, innerW, h - 2, bg);
    if (fill > 0) g.fillRect(kBatX + 1, y + 1, fill, h - 2, pct <= 15 ? TFT_RED : c);
}

}  // namespace

namespace header {

void draw(LovyanGFX &g, const char *title, const char *info, uint16_t bg, uint16_t titleColor,
          uint16_t infoColor)
{
    g.fillRect(0, 0, kScreenW, kH, bg);
    g.setFont(&fonts::AsciiFont8x16);
    g.setTextDatum(top_left);

    const hdrlayout::Fit f = hdrlayout::fit(title ? title : "", info ? info : "");
    if (!f.title.empty()) {
        g.setTextColor(titleColor, bg);
        g.drawString(f.title.c_str(), 0, 0);
    }
    if (!f.info.empty()) {
        g.setTextColor(infoColor, bg);
        g.drawString(f.info.c_str(), kTextRight - static_cast<int>(f.info.size()) * kCharW, 0);
    }

    // 蓝牙：未启动最暗；闪烁的灭相用暗灰而不是不画，位置一直看得见
    uint16_t btColor = kOffColor;
    switch (btState()) {
        case hdrlayout::BtIcon::Off:
            btColor = kOffColor;
            break;
        case hdrlayout::BtIcon::Dim:
            btColor = TFT_DARKGREY;
            break;
        case hdrlayout::BtIcon::Bright:
            btColor = TFT_WHITE;
            break;
    }
    drawBt(g, kBtX, btColor);

    const int lvl = volume::level();
    drawSpeaker(g, kSpkX, lvl == 0, TFT_DARKGREY);
    char v[2] = {static_cast<char>('0' + (lvl < 0 ? 0 : lvl > 9 ? 9 : lvl)), '\0'};
    g.setTextColor(TFT_WHITE, bg);
    g.drawString(v, kVolX, 0);

    drawBattery(g, gBatPct, TFT_DARKGREY, bg);
    g.setTextColor(TFT_WHITE, bg);
    g.drawString(hdrlayout::batteryDigits(gBatPct).c_str(), kBatPctX, 0);
}

bool tick()
{
    const uint32_t now = millis();
    if (!gBatRead || now - gBatMs >= kBatPollMs) {
        gBatRead = true;
        gBatMs = now;
        gBatPct = static_cast<int>(M5.Power.getBatteryLevel());
    }

    struct Snap {
        hdrlayout::BtIcon bt;
        int vol;
        int bat;
    };
    static Snap last = {hdrlayout::BtIcon::Off, -1, -2};
    const Snap cur = {btState(), volume::level(), gBatPct};
    const bool changed = cur.bt != last.bt || cur.vol != last.vol || cur.bat != last.bat;
    last = cur;
    return changed;
}

}  // namespace header
