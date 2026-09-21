#include "viz_app.h"

#include <vizmodel.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// 屏幕旋转后 240x135。布局（设计文档第 2 节）：
//   y 0..15    顶栏：左 = 曲号 + 首行预览，右 = 已播/总时长（暂停时加 II）
//   y 18..127  效果区
//   y 130..134 进度条
constexpr int kScreenW = 240;
constexpr int kCharW = 8;
constexpr int kCols = kScreenW / kCharW;  // 30

constexpr int kTopY = 0;
constexpr int kFxY = 18;
constexpr int kFxH = 110;
constexpr int kProgY = 130;
constexpr int kProgH = 5;

// 风格。每加一种就往这里追加一项并把 kStyleCount 加一 —— `.` 键按它取模循环。
enum class Style : uint8_t { Spectrum };
constexpr int kStyleCount = 1;

// 风格和配色只活在 RAM 里：下次播放沿用，重启归零（设计明确不落盘）
Style gStyle = Style::Spectrum;
int gPaletteIdx = 0;

char gTitle[48] = {0};
uint8_t gId = 0;
vizmodel::SpanSemi gSpan;  // begin() 时算一次，卷帘风格用

const vizmodel::Palette &palette()
{
    return vizmodel::kPalettes[gPaletteIdx];
}

// 当前音已经响了多久。没在播（index < 0）时给 0
uint32_t sinceOnset(const PlaybackFrame &f)
{
    if (f.index < 0 || f.elapsedMs <= f.onsetMs) return 0;
    return f.elapsedMs - f.onsetMs;
}

void drawTopBar(LovyanGFX &g, const PlaybackFrame &f)
{
    const vizmodel::Palette &p = palette();

    char el[8], to[8];
    vizmodel::formatMmSs(f.elapsedMs, el, sizeof(el));
    vizmodel::formatMmSs(f.totalMs, to, sizeof(to));

    char right[24];
    std::snprintf(right, sizeof(right), "%s%s/%s", f.paused ? "II " : "", el, to);
    const int rightCols = static_cast<int>(std::strlen(right));

    g.setTextColor(p.mid, TFT_BLACK);
    g.drawString(right, kScreenW - rightCols * kCharW, kTopY);

    // 左边按剩下的列数截断，别和右边的时间叠在一起
    char left[64];
    std::snprintf(left, sizeof(left), "%02u %s", static_cast<unsigned>(gId), gTitle);
    int leftCols = kCols - rightCols - 1;
    if (leftCols < 0) leftCols = 0;
    if (static_cast<int>(std::strlen(left)) > leftCols) left[leftCols] = '\0';

    g.setTextColor(p.dim, TFT_BLACK);
    g.drawString(left, 0, kTopY);
}

void drawProgress(LovyanGFX &g, const PlaybackFrame &f)
{
    const vizmodel::Palette &p = palette();

    g.fillRect(0, kProgY, kScreenW, kProgH, p.faint);
    const int w = vizmodel::progressWidth(f.elapsedMs, f.totalMs, kScreenW);
    if (w > 0) g.fillRect(0, kProgY, w, kProgH, p.bright);
}

// ── 风格 1：频谱柱 ──────────────────────────────────────────
void drawSpectrum(LovyanGFX &g, const PlaybackFrame &f)
{
    const vizmodel::Palette &p = palette();

    float h[vizmodel::kBarCount];
    // 没在播时按休止符画（只剩底噪），不去碰 f.freq
    vizmodel::barHeights(f.index >= 0 ? f.freq : 0.0f, sinceOnset(f), f.holdMs, h,
                         vizmodel::kBarCount);

    const int bw = kScreenW / vizmodel::kBarCount;  // 24 根 x 10px 正好铺满
    for (int i = 0; i < vizmodel::kBarCount; ++i) {
        int barH = static_cast<int>(std::lround(h[i] * kFxH));
        if (barH < 1) barH = 1;  // 底噪线：静止时不全黑
        if (barH > kFxH) barH = kFxH;

        g.fillRect(i * bw, kFxY + kFxH - barH, bw - 1, barH,
                   vizmodel::colorAt(p, vizmodel::levelOfIntensity(h[i])));
    }
}

}  // namespace

void viz_app::begin(const char *title, uint8_t id, const Player &player)
{
    // 拷进自己的固定缓冲并截断。用 snprintf 而不是 strlcpy：效果一样
    // （截断 + 保证 NUL），但不依赖 BSD 扩展在两个工具链里都在
    std::snprintf(gTitle, sizeof(gTitle), "%s", title ? title : "");
    gId = id;

    // 音域在这里算一次：卷帘每帧重扫全谱是浪费
    gSpan = vizmodel::scoreSemitoneSpan(player.score());
}

void viz_app::draw(LovyanGFX &g, const Player &player)
{
    const PlaybackFrame f = player.frame();  // 一次取齐，整帧用同一个 elapsed

    drawTopBar(g, f);
    drawProgress(g, f);

    switch (gStyle) {
        case Style::Spectrum:
            drawSpectrum(g, f);
            break;
    }
}

bool viz_app::handleKey(char c, Player &player)
{
    switch (c) {
        case '`':
            return false;  // 回曲库页

        case ' ':
            player.isPaused() ? player.resume() : player.pause();
            break;

        case '.':
            gStyle = static_cast<Style>((static_cast<int>(gStyle) + 1) % kStyleCount);
            break;

        case ',':
            gPaletteIdx = (gPaletteIdx + 1) % vizmodel::kPaletteCount;
            break;

        default:
            break;  // 其余按键忽略
    }

    return true;
}
