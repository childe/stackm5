// 全屏可视化的纯逻辑层 —— 零硬件依赖，可以在电脑上单元测试。
//
// 播放器是我们自己合成的音（谱面 → 频率 → 喇叭），不需要采集音频：每个音的
// 频率、起止时间、时值、BPM 拍点都能确定性地算出来。所以这里全是纯函数
// ——「给定同样输入必出同样结果」是硬要求，画面冻结（暂停）和单测都靠它。
//
// 设计见 docs/superpowers/specs/2026-09-21-fullscreen-visualizer-design.md
#pragma once

#include <jianpu.h>

#include <cstddef>
#include <cstdint>

namespace vizmodel {

// ── 配色 ────────────────────────────────────────────────────
// 一套 4 级亮度（RGB565），四种风格只从当前调色板取色，黑底。
// 单色系不会触发「高饱和多色混排刺眼」的问题（实机反馈只针对多色混排）。
struct Palette {
    uint16_t bright;
    uint16_t mid;
    uint16_t dim;
    uint16_t faint;
};

constexpr int kPaletteCount = 4;
extern const Palette kPalettes[kPaletteCount];

// level：0=bright 1=mid 2=dim 3=faint，越界钳制
uint16_t colorAt(const Palette &p, int level);

// 0..1 的强度 → 档位下标（0=bright … 3=faint）
int levelOfIntensity(float v);

// ── 顶栏 / 进度条的纯算术 ────────────────────────────────────
// "mm:ss"，最长 "99:59"（超过就钳住，不让格式串把缓冲撑爆）
void formatMmSs(uint32_t ms, char *out, size_t cap);

// 已播比例 → 前景条宽度。totalMs == 0 时返回 0（不除零）
int progressWidth(uint32_t elapsedMs, uint32_t totalMs, int fullW);

// ── 音高 ────────────────────────────────────────────────────
// 半音数一律相对中音 do。基准和 jianpu::noteToFreq 用的是同一个值 ——
// 半音数不自己照抄音阶表，而是从频率反算，保证和发声永远一致。
constexpr float kMiddleCFreq = 261.626f;
constexpr int kNoSemi = -1000;  // 休止符 / 无音高

int semitoneOfFreq(float freq);
int noteSemitone(const jianpu::Note &n, const jianpu::Header &h);

// 整首谱的音域。卷帘的纵轴归一化要用，begin() 时算一次就够
struct SpanSemi {
    int minSemi = 0;
    int maxSemi = 0;
};

// 全是休止符 / 空谱 → {0, 0}
SpanSemi scoreSemitoneSpan(const jianpu::Score &s);

// ── 风格 1：频谱柱 ──────────────────────────────────────────
constexpr int kBarCount = 24;      // 24 根柱铺满 240px
constexpr int kBarLowSemi = -12;   // 映射窗口下界 = C3（示波器也用这一对常量）
constexpr int kBarSemiSpan = 36;   // C3..C6，三个八度

constexpr float kNoiseFloor = 0.06f;       // 底噪线：静止时不全黑
constexpr float kNeighborFalloff = 0.45f;  // 每远一根柱乘这个
constexpr float kSpectrumTail = 0.15f;     // 时值末尾衰减到的比例

// 音高 → 柱下标。休止符或 barCount <= 0 返回 -1
int pitchToBar(float freq, int barCount);

// 时域包络：起始时刻 1.0，时值末尾 kSpectrumTail，指数衰减
float spectrumEnvelope(uint32_t sinceOnsetMs, uint32_t holdMs);

// 写 n 个 0..1 的柱高。休止符（freq <= 0）时全是底噪。
// 调用方给固定容量数组，这里只填不分配
void barHeights(float freq, uint32_t sinceOnsetMs, uint32_t holdMs, float *out, int n);

// ── 风格 2：音高卷帘 ────────────────────────────────────────
// 横轴 = 时间窗口 [t-pastMs, t+futureMs]，纵轴 = 音高。
// 「现在」是一条固定的竖线，方块从右往左流过它。
// 竖线位置由 pastMs / futureMs 算出来（rollNowX），不单独给字段 ——
// 左右两半必须是同一个 px/ms，否则滚动速度会在竖线处突变。
struct RollGeom {
    int x0 = 0;   // 效果区左边界（含）
    int y0 = 18;  // 效果区上边界（含）
    int w = 240;
    int h = 110;
    int blockH = 6;
    uint32_t pastMs = 2000;
    uint32_t futureMs = 4000;
};

enum class RollState : uint8_t { Past, Now, Future };

struct RollBlock {
    int x0 = 0;
    int x1 = 0;  // 右边界（含）
    int y = 0;
    RollState state = RollState::Future;
};

int rollNowX(const RollGeom &g);

// 半音数 → 方块上边缘 y。音域退化（minSemi == maxSemi）时返回效果区中线
int rollBlockY(int semi, SpanSemi span, const RollGeom &g);

// 把落在时间窗口里的音符写成方块，返回写入的个数（≤ cap），按时间顺序填充。
// 休止符留空（不产生方块）。调用方给固定容量数组、这里只填不分配：
// 每帧 30 次返回 std::vector 会在无 PSRAM 的 ESP32 上持续搅动堆。
//
// 窗口里的方块多过 cap 时**保留以「现在」为中心的那一段**，而不是填满前 cap
// 个就收手：300 BPM 的 0.125 拍音符是 25ms 一个，6000ms 的窗口里有 240 个方块、
// 竖线左边（过去 2000ms）就有 80 个 —— 按时间顺序填 64 个槽会在竖线左边就填满，
// 正在响的音被整个挤掉、竖线右边一片空白。保留段的左边界取
// `nowSlot - cap * pastMs / window`，让「现在」落在它在屏幕上该在的比例位置，
// 留白因此对称地落在窗口两端，而当前音永远在缓冲里。
int rollBlocks(const jianpu::Score &s, const jianpu::Timeline &t, uint32_t elapsedMs,
               const RollGeom &g, SpanSemi span, RollBlock *out, int cap);

}  // namespace vizmodel
