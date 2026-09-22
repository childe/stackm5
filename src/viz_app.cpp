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
enum class Style : uint8_t { Spectrum, Roll, Wave, BigNote };
constexpr int kStyleCount = 4;

// 风格和配色只活在 RAM 里：下次播放沿用，重启归零（设计明确不落盘）
Style gStyle = Style::Spectrum;
int gPaletteIdx = 0;

char gTitle[48] = {0};
uint8_t gId = 0;
vizmodel::SpanSemi gSpan;  // begin() 时算一次，卷帘风格用

// 卷帘的方块缓冲：固定容量、不每帧分配（无 PSRAM 的 ESP32 上每帧 30 次
// 返回 std::vector 会持续搅动堆）。容量按一屏最多画得下的方块数给 ——
// 效果区宽 240px、一个方块至少占 1px，所以 240 就是这个上限：300 BPM 的
// 0.125 拍音符（25ms 一个）一个窗口正好 240 个方块，到这个密度一个都不丢。
// 240 * sizeof(RollBlock) ≈ 3.8KB 静态 RAM，S3 上不心疼。比这还密的谱
// （方块不到 1px）由 rollBlocks 保留以「现在」为中心的一段，不会丢当前音
constexpr int kBlockCap = 240;
vizmodel::RollBlock gBlocks[kBlockCap];

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

// ── 风格 2：音高卷帘 ────────────────────────────────────────
void drawRoll(LovyanGFX &g, const Player &player, const PlaybackFrame &f)
{
    const vizmodel::Palette &p = palette();

    vizmodel::RollGeom geom;
    geom.x0 = 0;
    geom.y0 = kFxY;
    geom.w = kScreenW;
    geom.h = kFxH;

    // 「现在」是一条固定的竖线，方块从右往左流过它
    g.drawFastVLine(vizmodel::rollNowX(geom), kFxY, kFxH, p.faint);

    const int n = vizmodel::rollBlocks(player.score(), player.timeline(), f.elapsedMs, geom, gSpan,
                                       gBlocks, kBlockCap);
    for (int i = 0; i < n; ++i) {
        const vizmodel::RollBlock &b = gBlocks[i];

        uint16_t c = p.mid;  // 未播
        if (b.state == vizmodel::RollState::Now) {
            c = p.bright;
        } else if (b.state == vizmodel::RollState::Past) {
            c = p.dim;
        }

        g.fillRect(b.x0, b.y, b.x1 - b.x0 + 1, geom.blockH, c);
    }
}

// ── 风格 3：示波器 ─────────────────────────────────────────
// y(x) = A · sin(2π · cycles · x/W + phase)：只有 cycles 随音高变，
// A 在音符时值内衰减，phase 按固定角速度随 elapsed 匀速滚（与频率无关）
void drawWave(LovyanGFX &g, const PlaybackFrame &f)
{
    const vizmodel::Palette &p = palette();

    const int midY = kFxY + kFxH / 2;
    const float maxAmp = static_cast<float>(kFxH / 2 - 2);

    // 休止符 / 没在播 = 无激励 → 平线
    const bool excited = (f.index >= 0 && f.freq > 0.0f);
    const float amp = excited ? vizmodel::waveAmplitude(sinceOnset(f), f.holdMs) * maxAmp : 0.0f;
    const float cycles = vizmodel::waveCyclesOnScreen(excited ? f.freq : 0.0f);
    const float phase = vizmodel::wavePhase(f.elapsedMs);

    // 折线起点取 x=0 的真实波形值。拿中线当起点的话，第一段会从固定中线
    // 连到首个采样点，在屏幕左缘多画一条竖线 —— 相位滚到 sin≈±1 时最高
    // 有 amp（约 53px），看起来像一根固定的柱子
    int prevY = vizmodel::waveY(0, kScreenW, cycles, phase, amp, midY);
    for (int x = 1; x < kScreenW; ++x) {
        const int y = vizmodel::waveY(x, kScreenW, cycles, phase, amp, midY);

        // 高音时相邻像素的 y 差得远，画点会断成虚线，所以逐段连线
        g.drawLine(x - 1, prevY, x, y, p.bright);
        prevY = y;
    }
}

// ── 风格 4：大字简谱 ────────────────────────────────────────
// 数字 + 高低八度圆点，位图字体整数倍放大。cx 是水平中心
void drawGlyph(LovyanGFX &g, const vizmodel::NoteGlyph &gl, int cx, int y, int size,
               uint16_t color)
{
    if (!gl.valid) return;  // 下标越界（第一个音没有「前一个」）→ 不画

    const int w = kCharW * size;
    const int h = 16 * size;
    const int x = cx - w / 2;

    g.setFont(&fonts::AsciiFont8x16);
    g.setTextSize(static_cast<uint8_t>(size));
    g.setTextColor(color, TFT_BLACK);
    const char text[2] = {gl.digit, '\0'};
    g.drawString(text, x, y);
    g.setTextSize(1);  // 字号是全局状态，用完必须还原

    // 八度点：上方 = 高八度、下方 = 低八度，最多画两个
    const int dots = (gl.octave > 0) ? gl.octave : -gl.octave;
    const int r = (size >= 4) ? 3 : 2;
    for (int i = 0; i < dots && i < 2; ++i) {
        const int dy = (gl.octave > 0) ? y - (r + 1) - i * (2 * r + 2)
                                       : y + h + (r + 1) + i * (2 * r + 2);
        g.fillCircle(x + w / 2, dy, r, color);
    }
}

void drawBigNote(LovyanGFX &g, const Player &player, const PlaybackFrame &f)
{
    if (f.index < 0) return;  // 没在播：不画

    const vizmodel::Palette &p = palette();
    const jianpu::Score &s = player.score();

    // 拍点脉冲用亮度呼吸表达（拍首最亮、拍内衰减）。不用字号缩放：
    // 位图字号只能整数倍，缩放会跳。亮度只由 beatPhase 驱动，不画小节拍点圆
    const int level = vizmodel::beatLevel(vizmodel::beatPhase(f.elapsedMs, s.header.bpm));

    // 两侧淡色显示前一个 / 后一个音符
    drawGlyph(g, vizmodel::noteGlyphAt(s, f.index - 1), 40, kFxY + 40, 2, p.faint);
    drawGlyph(g, vizmodel::noteGlyphAt(s, f.index + 1), 200, kFxY + 40, 2, p.faint);

    // 正中超大显示当前音
    drawGlyph(g, vizmodel::noteGlyphAt(s, f.index), 120, kFxY + 15, 5,
              vizmodel::colorAt(p, level));
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
        case Style::Roll:
            drawRoll(g, player, f);
            break;
        case Style::Wave:
            drawWave(g, f);
            break;
        case Style::BigNote:
            drawBigNote(g, player, f);
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
