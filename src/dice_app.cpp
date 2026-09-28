#include "dice_app.h"

#include "volume.h"

#include <M5Cardputer.h>
#include <dice.h>
#include <dice_anim.h>
#include <esp_random.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr float kPi = 3.14159265f;

// 屏幕旋转后 240x135。布局：
//   y 0..15    顶栏：左 DICE，右 = 点数总和（停稳后才显示）+ 音量
//   y 17..111  骰子区：骰子底边落在 kGroundY，弹跳往上
//   y 112..127 按键提示
constexpr int kScreenW = 240;
constexpr int kCharW = 8;
constexpr int kTitleY = 0;
constexpr int kHintY = 112;
constexpr int kAreaY = 17;
constexpr int kAreaH = kHintY - kAreaY;
constexpr int kGroundY = 100;

constexpr int kMaxDice = 3;

// 每种颗数的边长和各颗的中心 x。转到 45° 时两个面加起来最宽是 √2 倍边长，
// 间距按这个留，停稳和翻面时相邻两颗不会叠在一起
struct Layout {
    int size;
    int cx[kMaxDice];
};
constexpr Layout kLayouts[kMaxDice] = {
    {64, {120, 0, 0}},
    {58, {70, 170, 0}},
    {50, {44, 120, 196}},
};

// 采样加速度计的间隔。BMI270 默认 100Hz 出数，再快只是重复读同一个值
constexpr uint32_t kImuSampleMs = 10;
// 动画的重绘间隔：约 40fps。实测画一帧约 2ms，瓶颈在推屏
constexpr uint32_t kFrameMs = 25;
// 摇晃时几颗骰子错开换面，不是整齐地一起跳
constexpr uint32_t kRattleOffsetMs = 23;

// 落面的「咔」：开机后第一次进这一页时合成一次。实机反馈方波「嘀」不好听 ——
// 它有稳定的音高，是电子音；真实的碰撞声没有音高，靠的是一个极短的噪声瞬态，
// 再带一点很快衰减的木头共鸣。共鸣放在 1.5kHz：这块小喇叭在 1~4kHz 才出声
constexpr uint32_t kKnockRate = 24000;
constexpr size_t kKnockLen = 432;  // 18ms
int8_t gKnock[kKnockLen];
bool gKnockReady = false;
// 固定用一个声道并打断上一声：几颗骰子一起落面时，叠起来只会糊成一片
constexpr int kKnockChannel = 0;

// Idle：静止显示结果。Rattle：手在晃。Tumble：出手后滚进来、落定
enum class Mode : uint8_t { Idle, Rattle, Tumble };
Mode gMode = Mode::Idle;
uint32_t gModeStartMs = 0;

int gCount = 2;  // 只活在 RAM 里，重启回到 2 颗
uint8_t gFace[kMaxDice] = {1, 1, 1};
uint32_t gSeed[kMaxDice] = {0, 0, 0};
uint8_t gLastFlip[kMaxDice] = {0, 0, 0};
uint32_t gLastRattleStep = 0;
uint32_t gLastFrameMs = 0;
uint32_t gLastSampleMs = 0;

bool gHasImu = false;
dice::ShakeDetector gShake;
dice::ThrowGate gGate;

// 灰度 0..255 → RGB565。骰子全用灰度，和其他 app 一致（这块屏上彩色太刺眼）
uint16_t grey(int v)
{
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return LovyanGFX::color565(v, v, v);
}

void buildKnock()
{
    uint32_t lcg = 0x2545f491u;  // 固定种子：每次开机是同一个声音
    for (size_t i = 0; i < kKnockLen; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kKnockRate);
        lcg = lcg * 1664525u + 1013904223u;
        const float noise = static_cast<float>(static_cast<int>(lcg >> 24) - 128) / 128.0f;

        const float body = std::sin(2.0f * kPi * 1500.0f * t) * std::exp(-t / 0.005f);
        const float hit = noise * std::exp(-t / 0.0015f);
        float v = 0.7f * body + 0.5f * hit;
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        gKnock[i] = static_cast<int8_t>(std::lround(v * 120.0f));
    }
}

void knock()
{
    // 每次音高随机偏一点（88%~112%），几颗骰子、几次碰撞听起来不是同一个声音
    const uint32_t rate = kKnockRate * (88 + esp_random() % 25) / 100;
    M5Cardputer.Speaker.playRaw(gKnock, kKnockLen, rate, false, 1, kKnockChannel, true);
}

void startRattle(uint32_t now)
{
    for (int i = 0; i < kMaxDice; ++i) gSeed[i] = esp_random();
    gLastRattleStep = 0;
    gLastFrameMs = 0;
    gModeStartMs = now;
    gMode = Mode::Rattle;
}

void startTumble(uint32_t now)
{
    // 结果现在就定。三颗都摇：中途按 1~3 改颗数时，新露出来的那颗也是新点数
    for (int i = 0; i < kMaxDice; ++i) {
        gFace[i] = dice::roll(esp_random());
        gSeed[i] = esp_random();
        gLastFlip[i] = 0;
    }
    gLastFrameMs = 0;
    gModeStartMs = now;
    gMode = Mode::Tumble;
}

// 这颗骰子自己的出手时间（减掉错开的出手延迟）
uint32_t tumbleElapsed(int i, uint32_t now)
{
    const uint32_t since = now - gModeStartMs;
    const uint32_t delay = dice::staggerFor(i);
    return since > delay ? since - delay : 0;
}

uint32_t rattleElapsed(int i, uint32_t now)
{
    return now - gModeStartMs + static_cast<uint32_t>(i) * kRattleOffsetMs;
}

// 画一个面。light 0..1 = 这个面朝向屏幕的程度：正对着最亮，侧过去变暗，
// 这样两个面贴在一起才看得出是立方体的两个面
void drawFace(LovyanGFX &g, int x, int y, int w, int h, int size, uint8_t face, float light)
{
    if (w < 1 || h < 1) return;

    g.fillRoundRect(x, y, w, h, size / 7, grey(90 + static_cast<int>(light * 140.0f)));

    // 太窄的侧面不画点：两三个像素宽的点只会是噪点
    if (w < size / 4 || h < size / 4) return;

    const uint16_t pipColor = grey(static_cast<int>((1.0f - light) * 60.0f));
    const float rx = static_cast<float>(w) * 0.09f;
    const float ry = static_cast<float>(h) * 0.09f;
    const uint16_t mask = dice::pipMask(face);

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (!((mask >> (row * 3 + col)) & 1u)) continue;
            // 三个点位在面上 25% / 50% / 75% 处
            const int px = x + (w * (1 + col)) / 4;
            const int py = y + (h * (1 + row)) / 4;
            g.fillEllipse(px, py, std::lround(rx) + 1, std::lround(ry) + 1, pipColor);
        }
    }
}

// 地上的影子：跳得越高越小。它不跟着跳，骰子「离地」才看得出来
void drawShadow(LovyanGFX &g, int cx, int size, int lift)
{
    g.fillEllipse(cx, kGroundY + 3, size / 2 + lift / 3, 3, grey(40));  // lift <= 0
}

void drawDie(LovyanGFX &g, int i, uint32_t now)
{
    const Layout &lay = kLayouts[gCount - 1];
    const int s = lay.size;

    if (gMode == Mode::Rattle) {
        // 骰盅里乱撞：原地抖、乱换面，稍微离地一点
        const dice::RattleFrame r = dice::rattleAt(gSeed[i], rattleElapsed(i, now));
        const int cx = lay.cx[i] + r.dx;
        drawShadow(g, cx, s, -4);
        drawFace(g, cx - s / 2, kGroundY - 4 + r.dy - s, s, s, s, r.face, 1.0f);
        return;
    }

    const dice::TumbleFrame f =
        gMode == Mode::Tumble ? dice::tumbleAt(gFace[i], gSeed[i], tumbleElapsed(i, now))
                              : dice::tumbleAt(gFace[i], gSeed[i], dice::kTumbleMs);

    const int cx = lay.cx[i] + f.shift;
    const int bottom = kGroundY + f.lift;
    drawShadow(g, cx, s, f.lift);

    if (f.from == f.to || f.turn <= 0.0f) {
        drawFace(g, cx - s / 2, bottom - s, s, s, s, f.from, 1.0f);
        return;
    }

    // 往右滚了 turn × 90°：转走的面被推到右边、按 cos 变窄；转来的面从左边
    // 翻上来、按 sin 变宽
    const float a = f.turn * 0.5f * kPi;
    const float cosA = std::cos(a);
    const float sinA = std::sin(a);
    const int fromW = static_cast<int>(std::lround(s * cosA));
    const int toW = static_cast<int>(std::lround(s * sinA));
    const int left = cx - (fromW + toW) / 2;
    drawFace(g, left, bottom - s, toW, s, s, f.to, sinA);
    drawFace(g, left + toW, bottom - s, fromW, s, s, f.from, cosA);
}

}  // namespace

void dice_app::begin()
{
    if (!gKnockReady) {
        buildKnock();
        gKnockReady = true;
    }
    gHasImu = M5.Imu.isEnabled();
    gShake.reset();
    gGate.reset();

    // 进来先滚一次：一进页面就有动静，也顺便告诉你它会动
    startTumble(millis());
}

bool dice_app::tick()
{
    const uint32_t now = millis();
    bool dirty = false;

    if (gHasImu && now - gLastSampleMs >= kImuSampleMs) {
        gLastSampleMs = now;
        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        // 单位是 g，静止时模长约 1g
        if (M5.Imu.getAccel(&ax, &ay, &az)) {
            const float dev = std::fabs(std::sqrt(ax * ax + ay * ay + az * az) - 1.0f);
            if (gShake.update(ax, ay, az, now)) {
                // 晃起来了：先在「骰盅」里乱撞，等手停了再出手。晃的时候人看不清
                // 屏幕，出手前就把动画放完的话，等你低头看只剩最后一下
                gGate.shake(now);
                if (gMode != Mode::Rattle) {
                    startRattle(now);
                    dirty = true;
                }
            } else if (gGate.update(dev, now)) {
                startTumble(now);
                dirty = true;
            }
        }
    }

    if (gMode == Mode::Idle) return dirty;

    if (gMode == Mode::Rattle) {
        // 只跟第一颗的节奏响：三颗都响的话一秒四十多声，是一片噪音
        const uint32_t step = dice::rattleAt(gSeed[0], rattleElapsed(0, now)).step;
        if (step != gLastRattleStep) {
            gLastRattleStep = step;
            knock();
        }
    } else {
        // 每颗骰子每落一次面响一声。一次 tick 最多响一声（同一声道会互相打断）
        bool knocked = false;
        for (int i = 0; i < gCount; ++i) {
            const uint8_t idx = dice::tumbleAt(gFace[i], gSeed[i], tumbleElapsed(i, now)).flipIndex;
            if (idx != gLastFlip[i]) {
                gLastFlip[i] = idx;
                if (!knocked) {
                    knock();
                    knocked = true;
                }
            }
        }

        if (now - gModeStartMs >= dice::rollDurationMs(gCount)) {
            gMode = Mode::Idle;  // 停稳，最后再画一帧就不再重绘
            return true;
        }
    }

    if (gLastFrameMs == 0 || now - gLastFrameMs >= kFrameMs) {
        gLastFrameMs = now;
        dirty = true;
    }
    return dirty;
}

void dice_app::draw(LovyanGFX &g)
{
    const uint32_t now = millis();

    // 滚进来时骰子一部分在屏幕外、转到 45° 时比边长高一截：裁在骰子区里，
    // 不压到顶栏和提示
    g.setClipRect(0, kAreaY, kScreenW, kAreaH);
    for (int i = 0; i < gCount; ++i) drawDie(g, i, now);
    g.clearClipRect();

    g.setTextColor(TFT_WHITE, TFT_BLACK);
    g.drawString("DICE", 0, kTitleY);

    // 总和只在停稳后显示：翻滚时的点数是装饰，显示出来反而误导
    char right[24];
    int n = 0;
    if (gMode == Mode::Idle && gCount > 1) {
        int sum = 0;
        for (int i = 0; i < gCount; ++i) sum += gFace[i];
        n = std::snprintf(right, sizeof(right), "sum %d  ", sum);
    } else if (!gHasImu) {
        n = std::snprintf(right, sizeof(right), "no IMU  ");
    }
    std::snprintf(right + n, sizeof(right) - n, "v%d", volume::level());
    g.setTextColor(TFT_DARKGREY, TFT_BLACK);
    g.drawString(right, kScreenW - static_cast<int>(std::strlen(right)) * kCharW, kTitleY);

    g.drawString(gHasImu ? "shake/SPC roll 1-3 =-vol `back" : "SPC roll 1-3 dice =-vol `back", 0,
                 kHintY);
}

bool dice_app::handleKey(char c)
{
    switch (c) {
        case '`':
            return false;  // 回菜单

        case ' ':
            // 按键是看着屏幕按的，不用等「手停」：直接出手。滚到一半再按也行
            gGate.reset();
            startTumble(millis());
            break;

        case '1':
        case '2':
        case '3':
            // 只改颗数不重摇：几颗骰子的点数早就摇好了，直接露出来
            gCount = c - '0';
            break;

        // 和其他页一致：键面上就是加减号。调完响一声，当场听到新音量
        case '=':
            volume::up();
            knock();
            break;

        case '-':
            volume::down();
            knock();
            break;

        default:
            break;  // 其余按键忽略
    }

    return true;
}
