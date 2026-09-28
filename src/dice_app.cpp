#include "dice_app.h"

#include <M5Cardputer.h>
#include <dice.h>
#include <dice_anim.h>
#include <esp_random.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// 屏幕旋转后 240x135。布局：
//   y 0..15    顶栏：左 DICE，右 = 点数总和（停稳后才显示）
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
// 间距按这个留，翻滚时相邻两颗不会叠在一起
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
// 翻滚时的重绘间隔：约 30fps，和可视化页一样
constexpr uint32_t kFrameMs = 33;

// 翻面的「嗒」：一小段方波，比正弦脆。幅度没拉满，免得满音量时太扎耳
constexpr uint8_t kClickWave[8] = {200, 200, 200, 200, 56, 56, 56, 56};
constexpr uint32_t kClickMs = 8;
// 固定用一个声道并打断上一声：三颗骰子一起翻时「嗒」会很密，叠起来只会糊成一片
constexpr int kClickChannel = 0;

int gCount = 2;  // 只活在 RAM 里，重启回到 2 颗
uint8_t gFace[kMaxDice] = {1, 1, 1};
uint32_t gSeed[kMaxDice] = {0, 0, 0};
uint8_t gLastFlip[kMaxDice] = {0, 0, 0};
bool gRolling = false;
uint32_t gRollStartMs = 0;
uint32_t gLastFrameMs = 0;
uint32_t gLastSampleMs = 0;

bool gHasImu = false;
dice::ShakeDetector gShake;

// 灰度 0..255 → RGB565。骰子全用灰度，和其他 app 一致（这块屏上彩色太刺眼）
uint16_t grey(int v)
{
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return LovyanGFX::color565(v, v, v);
}

void startRoll()
{
    // 结果现在就定。三颗都摇：中途按 1~3 改颗数时，新露出来的那颗也是新点数
    for (int i = 0; i < kMaxDice; ++i) {
        gFace[i] = dice::roll(esp_random());
        gSeed[i] = esp_random();
        gLastFlip[i] = 0;
    }
    gRollStartMs = millis();
    gLastFrameMs = 0;
    gRolling = true;
}

void click(int dieIndex)
{
    // 每颗音高略有不同，听起来是几颗骰子而不是一个节拍器
    const float freq = 1500.0f + 350.0f * static_cast<float>(dieIndex);
    M5Cardputer.Speaker.tone(freq, kClickMs, kClickChannel, true, kClickWave, sizeof(kClickWave));
}

// 这颗骰子在这次摇动里自己的时间（减掉错开的起步延迟）
uint32_t dieElapsed(int i, uint32_t now)
{
    const uint32_t sinceStart = now - gRollStartMs;
    const uint32_t delay = dice::staggerFor(i);
    return sinceStart > delay ? sinceStart - delay : 0;
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

void drawDie(LovyanGFX &g, int i, uint32_t now)
{
    const Layout &lay = kLayouts[gCount - 1];
    const int s = lay.size;

    const dice::TumbleFrame f =
        gRolling ? dice::tumbleAt(gFace[i], gSeed[i], dieElapsed(i, now))
                 : dice::tumbleAt(gFace[i], gSeed[i], dice::kTumbleMs);

    const int cx = lay.cx[i] + f.shift;
    const int bottom = kGroundY + f.lift;

    // 地上的影子：跳得越高越小。它不跟着跳，骰子「离地」才看得出来
    const int shadowRx = s / 2 + f.lift / 3;  // lift 是负数
    g.fillEllipse(lay.cx[i], kGroundY + 3, shadowRx, 3, grey(40));

    if (f.settled || f.turn <= 0.0f) {
        drawFace(g, cx - s / 2, bottom - s, s, s, s, f.from, 1.0f);
        return;
    }

    // 转了 turn × 90°：转走的面按 cos 变窄，转来的面按 sin 变宽
    const float a = f.turn * 1.5707963f;
    const float cosA = std::cos(a);
    const float sinA = std::sin(a);
    const int fromLen = static_cast<int>(std::lround(s * cosA));
    const int toLen = static_cast<int>(std::lround(s * sinA));

    if (f.vertical) {
        // 上下翻：转来的面在上，往下压过来。绕骰子中心转，所以上下各伸出一截；
        // 贴着地面往上长的话，45° 时会顶出骰子区被裁掉一大块
        const int total = fromLen + toLen;
        const int top = bottom - s / 2 - total / 2;
        drawFace(g, cx - s / 2, top, s, toLen, s, f.to, sinA);
        drawFace(g, cx - s / 2, top + toLen, s, fromLen, s, f.from, cosA);
    } else {
        // 左右翻：转来的面在左，往右推过去
        const int total = fromLen + toLen;
        const int left = cx - total / 2;
        drawFace(g, left, bottom - s, toLen, s, s, f.to, sinA);
        drawFace(g, left + toLen, bottom - s, fromLen, s, s, f.from, cosA);
    }
}

}  // namespace

void dice_app::begin()
{
    gHasImu = M5.Imu.isEnabled();
    gShake.reset();

    // 进来先摇一次：一进页面就有动静，也顺便告诉你它会动
    startRoll();
}

bool dice_app::tick()
{
    const uint32_t now = millis();
    bool dirty = false;

    if (gHasImu && now - gLastSampleMs >= kImuSampleMs) {
        gLastSampleMs = now;
        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        // 单位是 g。ShakeDetector 自带防抖：摇一下只算一次，晃着不停也至少隔 450ms
        if (M5.Imu.getAccel(&ax, &ay, &az) && gShake.update(ax, ay, az, now)) {
            startRoll();
            dirty = true;
        }
    }

    if (!gRolling) return dirty;

    // 每颗骰子每落一次面响一声。一次 tick 最多响一声（同一声道会互相打断）
    bool clicked = false;
    for (int i = 0; i < gCount; ++i) {
        const uint8_t idx = dice::tumbleAt(gFace[i], gSeed[i], dieElapsed(i, now)).flipIndex;
        if (idx != gLastFlip[i]) {
            gLastFlip[i] = idx;
            if (!clicked) {
                click(i);
                clicked = true;
            }
        }
    }

    if (now - gRollStartMs >= dice::rollDurationMs(gCount)) {
        gRolling = false;  // 停稳，最后再画一帧就不再重绘
        return true;
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

    // 骰子转到 45° 时会比边长高出一截，裁在骰子区里，不压到顶栏和提示
    g.setClipRect(0, kAreaY, kScreenW, kAreaH);
    for (int i = 0; i < gCount; ++i) drawDie(g, i, now);
    g.clearClipRect();

    g.setTextColor(TFT_WHITE, TFT_BLACK);
    g.drawString("DICE", 0, kTitleY);

    // 总和只在停稳后显示：翻滚时的点数是装饰，显示出来反而误导
    char right[16] = {0};
    if (!gRolling && gCount > 1) {
        int sum = 0;
        for (int i = 0; i < gCount; ++i) sum += gFace[i];
        std::snprintf(right, sizeof(right), "sum %d", sum);
    } else if (!gHasImu) {
        std::snprintf(right, sizeof(right), "no IMU");
    }
    g.setTextColor(TFT_DARKGREY, TFT_BLACK);
    g.drawString(right, kScreenW - static_cast<int>(std::strlen(right)) * kCharW, kTitleY);

    g.drawString(gHasImu ? "shake/SPC roll 1-3 dice `back" : "SPC roll  1-3 dice  `back", 0,
                 kHintY);
}

bool dice_app::handleKey(char c)
{
    switch (c) {
        case '`':
            return false;  // 回菜单

        case ' ':
            startRoll();  // 翻到一半再按也行：重新摇
            break;

        case '1':
        case '2':
        case '3':
            // 只改颗数不重摇：几颗骰子的点数早就摇好了，直接露出来
            gCount = c - '0';
            break;

        default:
            break;  // 其余按键忽略
    }

    return true;
}
