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

}  // namespace vizmodel
