#include "menu_app.h"

#include <M5Cardputer.h>
#include <carousel.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// 屏幕旋转后 240x135。布局：
//   y 0..15     顶栏：左 STACKM5，右 电量
//   y 20..95    中间项的描边框（x 82..157），item 从框里滑进滑出
//   y 46        图标中心
//   y 74..89    标签
//   y 104       位置指示点
//   y 119..134  按键提示（贴底）
constexpr int kScreenW = 240;
constexpr int kScreenH = 135;
constexpr int kCharW = 8;
constexpr int kCharH = 16;
constexpr int kTitleY = 0;
constexpr int kFrameX = 82;
constexpr int kFrameY = 20;
constexpr int kFrameW = 76;
constexpr int kFrameH = 76;
constexpr int kIconY = 46;
constexpr int kLabelY = 74;
constexpr int kDotsY = 104;
constexpr int kHintY = kScreenH - kCharH;

constexpr uint32_t kFrameMs = 16;  // 动画期间约 60fps

using IconFn = void (*)(LovyanGFX &g, int cx, int cy, int s, uint16_t c);

// 图标都按边长 s 等比例画，只用基本图元，不带位图。
// 线宽随尺寸走：正中 40px 用 3px，两侧 28px 用 2px

int stroke(int s)
{
    return s >= 36 ? 3 : 2;
}

int pct(int s, float f)
{
    return static_cast<int>(s * f + (f >= 0 ? 0.5f : -0.5f));
}

void thickRect(LovyanGFX &g, int x, int y, int w, int h, int r, int t, uint16_t c)
{
    for (int i = 0; i < t; ++i) {
        g.drawRoundRect(x + i, y + i, w - 2 * i, h - 2 * i, r > i ? r - i : 0, c);
    }
}

void thickLine(LovyanGFX &g, int x0, int y0, int x1, int y1, int t, uint16_t c)
{
    for (int i = 0; i < t; ++i) {
        g.drawLine(x0, y0 + i, x1, y1 + i, c);
    }
}

// 两个连着横梁的八分音符
void iconMusic(LovyanGFX &g, int cx, int cy, int s, uint16_t c)
{
    const int t = stroke(s);
    const int rx = pct(s, 0.15f);
    const int ry = pct(s, 0.11f);
    const int h1x = cx - pct(s, 0.24f);
    const int h1y = cy + pct(s, 0.30f);
    const int h2x = cx + pct(s, 0.22f);
    const int h2y = cy + pct(s, 0.22f);
    g.fillEllipse(h1x, h1y, rx, ry, c);
    g.fillEllipse(h2x, h2y, rx, ry, c);

    // 符干贴着符头右沿往上，顶到横梁
    const int s1x = h1x + rx - t;
    const int s2x = h2x + rx - t;
    const int top1 = cy - pct(s, 0.34f);
    const int top2 = cy - pct(s, 0.42f);
    g.fillRect(s1x, top1, t, h1y - top1, c);
    g.fillRect(s2x, top2, t, h2y - top2, c);

    // 横梁：从左符干顶斜到右符干顶
    const int bh = pct(s, 0.12f);
    const int bx1 = s2x + t;
    g.fillTriangle(s1x, top1, bx1, top2, bx1, top2 + bh, c);
    g.fillTriangle(s1x, top1, s1x, top1 + bh, bx1, top2 + bh, c);
}

// 摊开的书：两页 + 每页三行字
void iconVocab(LovyanGFX &g, int cx, int cy, int s, uint16_t c)
{
    const int t = stroke(s);
    const int pw = pct(s, 0.44f);
    const int ph = pct(s, 0.62f);
    const int top = cy - ph / 2;
    thickRect(g, cx - pw, top, pw + t / 2 + 1, ph, 2, t, c);
    thickRect(g, cx - t / 2, top, pw + t / 2 + 1, ph, 2, t, c);

    const int inset = pct(s, 0.09f);
    for (int i = 0; i < 3; ++i) {
        const int y = top + pct(s, 0.17f) + i * pct(s, 0.14f);
        g.drawFastHLine(cx - pw + inset, y, pw - 2 * inset, c);
        g.drawFastHLine(cx + inset, y, pw - 2 * inset, c);
    }
}

// 带天线和底座的电视
void iconRemote(LovyanGFX &g, int cx, int cy, int s, uint16_t c)
{
    const int t = stroke(s);
    const int w = pct(s, 0.90f);
    const int h = pct(s, 0.56f);
    const int top = cy - pct(s, 0.26f);
    thickRect(g, cx - w / 2, top, w, h, 3, t, c);

    // 天线
    const int antY = top - pct(s, 0.20f);
    g.drawLine(cx, top, cx - pct(s, 0.18f), antY, c);
    g.drawLine(cx + 1, top, cx - pct(s, 0.18f) + 1, antY, c);
    g.drawLine(cx, top, cx + pct(s, 0.18f), antY, c);
    g.drawLine(cx - 1, top, cx + pct(s, 0.18f) - 1, antY, c);

    // 底座
    const int bottom = top + h;
    g.fillRect(cx - t / 2, bottom, t, pct(s, 0.10f), c);
    g.fillRect(cx - pct(s, 0.22f), bottom + pct(s, 0.10f), pct(s, 0.44f), t, c);
}

// 骰子五点
void iconDice(LovyanGFX &g, int cx, int cy, int s, uint16_t c)
{
    const int t = stroke(s);
    const int w = pct(s, 0.80f);
    thickRect(g, cx - w / 2, cy - w / 2, w, w, pct(s, 0.15f), t, c);

    const int d = pct(s, 0.20f);
    const int r = s >= 36 ? 3 : 2;
    g.fillCircle(cx, cy, r, c);
    g.fillCircle(cx - d, cy - d, r, c);
    g.fillCircle(cx + d, cy - d, r, c);
    g.fillCircle(cx - d, cy + d, r, c);
    g.fillCircle(cx + d, cy + d, r, c);
}

// 分贝表盘：上半圆弧 + 5 道刻度 + 指向右上（偏响）的指针。
// 初版是 5 根音量柱，实机看像信号格，认不出是噪音计。
// 圆弧自己按三角函数画，不用 drawArc，免得依赖它的角度约定：
// 屏幕 y 向下，θ 取 180°..360° 正好是上半圆
void iconNoise(LovyanGFX &g, int cx, int cy, int s, uint16_t c)
{
    constexpr float kDeg = 3.14159265f / 180.0f;
    const int t = stroke(s);
    const int px = cx;
    const int py = cy + pct(s, 0.22f);
    const float r = s * 0.42f;

    const auto at = [&](float deg, float radius, int &x, int &y) {
        x = px + static_cast<int>(std::lround(radius * std::cos(deg * kDeg)));
        y = py + static_cast<int>(std::lround(radius * std::sin(deg * kDeg)));
    };

    for (int k = 0; k < t; ++k) {
        for (int deg = 180; deg < 360; deg += 10) {
            int x0, y0, x1, y1;
            at(static_cast<float>(deg), r - k, x0, y0);
            at(static_cast<float>(deg + 10), r - k, x1, y1);
            g.drawLine(x0, y0, x1, y1, c);
        }
    }

    static const float kTicks[] = {200.0f, 235.0f, 270.0f, 305.0f, 340.0f};
    for (const float deg : kTicks) {
        int x0, y0, x1, y1;
        at(deg, r * 0.72f, x0, y0);
        at(deg, r, x1, y1);
        g.drawLine(x0, y0, x1, y1, c);
    }

    int nx, ny;
    at(315.0f, r * 0.85f, nx, ny);
    thickLine(g, px, py, nx, ny, t - 1, c);
    g.fillCircle(px, py, s >= 36 ? 3 : 2, c);
}

// 圆角框里一条心电图折线
void iconDiag(LovyanGFX &g, int cx, int cy, int s, uint16_t c)
{
    const int t = stroke(s);
    const int w = pct(s, 0.90f);
    const int h = pct(s, 0.70f);
    thickRect(g, cx - w / 2, cy - h / 2, w, h, 3, t, c);

    static const float kPts[][2] = {{-0.34f, 0.0f},  {-0.14f, 0.0f}, {-0.07f, -0.22f},
                                    {0.03f, 0.20f},  {0.10f, -0.08f}, {0.16f, 0.0f},
                                    {0.34f, 0.0f}};
    constexpr int kN = sizeof(kPts) / sizeof(kPts[0]);
    for (int i = 0; i + 1 < kN; ++i) {
        thickLine(g, cx + pct(s, kPts[i][0]), cy + pct(s, kPts[i][1]) - t / 2,
                  cx + pct(s, kPts[i + 1][0]), cy + pct(s, kPts[i + 1][1]) - t / 2, t - 1, c);
    }
}

struct Item {
    const char *label;
    uint16_t color;
    IconFn icon;
    menu_app::App app;
};

// 显示顺序即 1-6 的顺序。DIAG 永远放最后：它是查问题用的，不是日常 app，
// 新 app 插在它前面（README 顶部的菜单说明一起改）
const Item kItems[] = {
    {"MUSIC", TFT_CYAN, iconMusic, menu_app::App::Music},
    {"VOCAB", TFT_YELLOW, iconVocab, menu_app::App::Vocab},
    {"TV REMOTE", TFT_GREEN, iconRemote, menu_app::App::Remote},
    {"DICE", TFT_ORANGE, iconDice, menu_app::App::Dice},
    {"NOISE", TFT_MAGENTA, iconNoise, menu_app::App::Noise},
    {"DIAG", TFT_LIGHTGREY, iconDiag, menu_app::App::Diag},
};
constexpr int kItemCount = sizeof(kItems) / sizeof(kItems[0]);

// 开机当前项是 VOCAB，第一屏就是 MUSIC / VOCAB / TV REMOTE、MUSIC 在最左。
// 当前项若是 MUSIC，左边露出的是绕回来的 DIAG，开机第一眼很怪（实机反馈）
carousel::Carousel gCar(kItemCount, 1);

void drawRightAligned(LovyanGFX &g, const char *s, int y, uint16_t color)
{
    g.setTextColor(color, TFT_BLACK);
    g.drawString(s, kScreenW - static_cast<int>(std::strlen(s)) * kCharW, y);
}

void drawTitle(LovyanGFX &g)
{
    g.setTextColor(TFT_WHITE, TFT_BLACK);
    g.drawString("STACKM5", 0, kTitleY);

    // 电量。Cardputer-Adv 没有电量计芯片，是 ADC1 GPIO10 读分压（_adc_ratio=2.0），
    // M5Unified 再按 (mv-3300)/8 线性折算成百分比 —— 锂电的真实曲线中段很平，
    // 所以这个数会在高位赖很久、然后掉得很快。电压一起显示出来，它才是可诊断的那个量。
    const int mv = M5.Power.getBatteryVoltage();
    char bat[20];
    std::snprintf(bat, sizeof(bat), "%d%% %d.%02dV",
                  static_cast<int>(M5.Power.getBatteryLevel()), mv / 1000, (mv % 1000) / 10);
    drawRightAligned(g, bat, kTitleY, TFT_DARKGREY);
}

}  // namespace

namespace menu_app {

void draw(LovyanGFX &g)
{
    drawTitle(g);

    // 描边框固定不动，先画，滑动中的图标可以压在它上面
    g.drawRoundRect(kFrameX, kFrameY, kFrameW, kFrameH, 6, TFT_DARKGREY);

    const float off = gCar.offset(millis());
    for (int i = 0; i < kItemCount; ++i) {
        const float pos = carousel::slotPos(i, gCar.selected(), off, kItemCount);
        const carousel::SlotGeom geom = carousel::slotGeom(pos);
        if (!geom.visible) continue;

        const uint16_t c = carousel::dim565(kItems[i].color, geom.bright);
        kItems[i].icon(g, geom.x, kIconY, geom.size, c);

        g.setTextDatum(top_center);
        g.setTextColor(c, TFT_BLACK);
        g.drawString(kItems[i].label, geom.x, kLabelY);
        g.setTextDatum(top_left);
    }

    // 指示点跟 selected 走（按下就跳），不跟着画面滑
    for (int i = 0; i < kItemCount; ++i) {
        const int x = 95 + 10 * i;
        if (i == gCar.selected()) {
            g.fillCircle(x, kDotsY, 2, TFT_WHITE);
        } else {
            g.drawCircle(x, kDotsY, 2, TFT_DARKGREY);
        }
    }

    // 背单词页两页都排满了，放不下按键提示，所以提示写在入口这里
    g.setTextColor(TFT_DARKGREY, TFT_BLACK);
    const bool vocab = kItems[gCar.selected()].app == App::Vocab;
    // 开机就停在 VOCAB，所以这一句也得带上转动键，否则第一屏看不出怎么转
    g.drawString(vocab ? "SPC flip ENT skip ,/ move" : ",/ move SPC/ENT open 1-6", 0, kHintY);
}

bool tick()
{
    static uint32_t lastMs = 0;
    static bool wasAnimating = false;

    const uint32_t now = millis();
    if (!gCar.animating(now)) {
        // 动画在两帧之间停下时补画一次，否则最后一帧停在 offset ≠ 0 上
        const bool finalFrame = wasAnimating;
        wasAnimating = false;
        return finalFrame;
    }
    wasAnimating = true;
    if (now - lastMs < kFrameMs) return false;
    lastMs = now;
    return true;
}

App handleKey(char c)
{
    if (c == ',') {
        gCar.step(-1, millis());
        return App::None;
    }
    if (c == '/') {
        gCar.step(+1, millis());
        return App::None;
    }
    if (c == ' ' || c == '\n') return kItems[gCar.selected()].app;
    if (c >= '1' && c < '1' + kItemCount) {
        // 同时把当前项设成这一项，返回首页时它正好停在中间
        gCar.jump(c - '1');
        return kItems[gCar.selected()].app;
    }
    return App::None;
}

}  // namespace menu_app
