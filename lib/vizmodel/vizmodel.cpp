#include "vizmodel.h"

#include <cmath>
#include <cstdio>

namespace vizmodel {

// 四套单色调色板（RGB565）。灰度那套用和背单词页一样的四个灰阶值。
const Palette kPalettes[kPaletteCount] = {
    {0xFFFF, 0xD69A, 0x9492, 0x738E},  // Gray：白→灰，与背单词一致
    {0x07FF, 0x0618, 0x0451, 0x02CB},  // Cyan：黑底青，示波器味
    {0xFDE0, 0xCCA0, 0x8B40, 0x5A00},  // Amber：黑底橙黄，老终端味
    {0x07E0, 0x0600, 0x0440, 0x02C0},  // Green：黑底绿，CRT 味
};

uint16_t colorAt(const Palette &p, int level)
{
    const int lv = (level < 0) ? 0 : ((level > 3) ? 3 : level);
    switch (lv) {
        case 0:
            return p.bright;
        case 1:
            return p.mid;
        case 2:
            return p.dim;
        default:
            return p.faint;
    }
}

int levelOfIntensity(float v)
{
    if (v >= 0.66f) return 0;
    if (v >= 0.33f) return 1;
    if (v >= 0.10f) return 2;
    return 3;
}

void formatMmSs(uint32_t ms, char *out, size_t cap)
{
    if (out == nullptr || cap == 0) return;

    const uint32_t total = ms / 1000;
    uint32_t mm = total / 60;
    uint32_t ss = total % 60;
    if (mm > 99) {  // 曲库里不会有这么长的曲子，但格式串的宽度得有保证
        mm = 99;
        ss = 59;
    }

    std::snprintf(out, cap, "%02u:%02u", static_cast<unsigned>(mm), static_cast<unsigned>(ss));
}

int progressWidth(uint32_t elapsedMs, uint32_t totalMs, int fullW)
{
    if (totalMs == 0 || fullW <= 0) return 0;
    if (elapsedMs > totalMs) elapsedMs = totalMs;

    // 先乘后除，用 64 位避免 240 * 几十万毫秒溢出
    return static_cast<int>(static_cast<uint64_t>(fullW) * elapsedMs / totalMs);
}

int semitoneOfFreq(float freq)
{
    if (freq <= 0.0f) return kNoSemi;
    return static_cast<int>(std::lround(12.0f * std::log2(freq / kMiddleCFreq)));
}

int noteSemitone(const music::Note &n, const music::Header &h)
{
    return semitoneOfFreq(music::noteToFreq(n, h));
}

SpanSemi scoreSemitoneSpan(const music::Score &s)
{
    SpanSemi span;
    bool any = false;

    for (const music::Note &n : s.notes) {
        const int semi = noteSemitone(n, s.header);
        if (semi == kNoSemi) continue;  // 休止符不参与音域

        if (!any) {
            span.minSemi = semi;
            span.maxSemi = semi;
            any = true;
            continue;
        }
        if (semi < span.minSemi) span.minSemi = semi;
        if (semi > span.maxSemi) span.maxSemi = semi;
    }

    return span;
}

int pitchToBar(float freq, int barCount)
{
    if (barCount <= 0) return -1;

    const int semi = semitoneOfFreq(freq);
    if (semi == kNoSemi) return -1;

    int idx = static_cast<int>(std::floor(static_cast<float>(semi - kBarLowSemi) *
                                          static_cast<float>(barCount) /
                                          static_cast<float>(kBarSemiSpan)));
    if (idx < 0) idx = 0;
    if (idx > barCount - 1) idx = barCount - 1;
    return idx;
}

float spectrumEnvelope(uint32_t sinceOnsetMs, uint32_t holdMs)
{
    if (holdMs == 0) return kSpectrumTail;  // 退化的时值：当作已经衰减到底

    const float t = (sinceOnsetMs >= holdMs)
                        ? 1.0f
                        : static_cast<float>(sinceOnsetMs) / static_cast<float>(holdMs);

    // pow(tail, t)：t=0 → 1，t=1 → tail，中间是指数曲线
    return std::pow(kSpectrumTail, t);
}

void barHeights(float freq, uint32_t sinceOnsetMs, uint32_t holdMs, float *out, int n)
{
    if (out == nullptr || n <= 0) return;

    for (int i = 0; i < n; ++i) out[i] = kNoiseFloor;

    const int bar = pitchToBar(freq, n);
    if (bar < 0) return;  // 休止符 = 无激励，只留底噪

    const float env = spectrumEnvelope(sinceOnsetMs, holdMs);
    for (int i = 0; i < n; ++i) {
        const int d = (i > bar) ? (i - bar) : (bar - i);
        const float v = env * std::pow(kNeighborFalloff, static_cast<float>(d));
        if (v > out[i]) out[i] = v;
    }
}

int rollNowX(const RollGeom &g)
{
    const uint32_t window = g.pastMs + g.futureMs;
    if (window == 0) return g.x0;

    return g.x0 + static_cast<int>(std::lround(static_cast<double>(g.w) *
                                               static_cast<double>(g.pastMs) /
                                               static_cast<double>(window)));
}

int rollBlockY(int semi, SpanSemi span, const RollGeom &g)
{
    const int usable = g.h - g.blockH;
    if (span.maxSemi <= span.minSemi) {
        return g.y0 + usable / 2;  // 音域只有一个半音：固定画在中线
    }

    float t = static_cast<float>(span.maxSemi - semi) /
              static_cast<float>(span.maxSemi - span.minSemi);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    return g.y0 + static_cast<int>(std::lround(t * static_cast<float>(usable)));
}

namespace {

// 一个音符在卷帘窗口里的横向范围（像素，右边界含）。返回 false = 不在窗口里。
// 只被 rollBlocks 调用，它已经保证了 window > 0 && g.w > 0。
bool rollSpanOf(uint32_t onset, uint32_t hold, uint32_t elapsedMs, const RollGeom &g, int &bx0,
                int &bx1)
{
    const uint32_t window = g.pastMs + g.futureMs;
    const float pxPerMs = static_cast<float>(g.w) / static_cast<float>(window);
    const int rightEdge = g.x0 + g.w - 1;

    // 全部换成 float 再减，避免无符号相减回绕成天文数字
    const float leftMs = static_cast<float>(onset) - static_cast<float>(elapsedMs) +
                         static_cast<float>(g.pastMs);
    const float rightMs = leftMs + static_cast<float>(hold);

    if (rightMs < 0.0f) return false;                       // 已经滚出左边
    if (leftMs > static_cast<float>(window)) return false;  // 还没进右边

    bx0 = g.x0 + static_cast<int>(std::lround(leftMs * pxPerMs));
    bx1 = g.x0 + static_cast<int>(std::lround(rightMs * pxPerMs));
    if (bx1 < bx0) bx1 = bx0;  // 极短的音至少占 1px
    if (bx0 < g.x0) bx0 = g.x0;
    if (bx1 > rightEdge) bx1 = rightEdge;
    return bx1 >= g.x0 && bx0 <= rightEdge;
}

}  // namespace

int rollBlocks(const music::Score &s, const music::Timeline &t, uint32_t elapsedMs,
               const RollGeom &g, SpanSemi span, RollBlock *out, int cap)
{
    if (out == nullptr || cap <= 0) return 0;

    const uint32_t window = g.pastMs + g.futureMs;
    if (window == 0 || g.w <= 0) return 0;

    size_t count = s.notes.size();
    if (t.onsetMs.size() < count) count = t.onsetMs.size();
    if (t.holdMs.size() < count) count = t.holdMs.size();

    // 第一遍：数窗口里有多少个有音高的音，并记下最后一个已经起音的是第几个。
    // 便宜的窗口测试放前面、pow/log2 的 noteSemitone 只对窗口内的音算 ——
    // 两遍扫的额外代价就只有一遍纯浮点算术，长谱也吃得住。
    int total = 0;
    int nowSlot = 0;
    for (size_t i = 0; i < count; ++i) {
        int bx0 = 0, bx1 = 0;
        if (!rollSpanOf(t.onsetMs[i], t.holdMs[i], elapsedMs, g, bx0, bx1)) continue;
        if (noteSemitone(s.notes[i], s.header) == kNoSemi) continue;  // 休止符留空

        if (elapsedMs >= t.onsetMs[i]) nowSlot = total;
        ++total;
    }

    // 装不下就把保留段挪到「现在」周围：给它前面留 cap * pastMs / window 个位置，
    // 正好是竖线在屏幕上的比例位置。三条钳制之后 nowSlot - skip 必落在 [0, cap)，
    // 所以当前音永远画得出来。
    int skip = 0;
    if (total > cap) {
        const int before = static_cast<int>(static_cast<uint64_t>(cap) *
                                            static_cast<uint64_t>(g.pastMs) / window);
        skip = nowSlot - before;
        if (skip > total - cap) skip = total - cap;
        if (skip < 0) skip = 0;
    }

    // 第二遍：跳过前 skip 个，最多填 cap 个。筛选条件必须和第一遍逐字一致，
    // 否则 slot 的编号对不上 skip
    int slot = 0;
    int n = 0;
    for (size_t i = 0; i < count && n < cap; ++i) {
        int bx0 = 0, bx1 = 0;
        if (!rollSpanOf(t.onsetMs[i], t.holdMs[i], elapsedMs, g, bx0, bx1)) continue;

        const int semi = noteSemitone(s.notes[i], s.header);
        if (semi == kNoSemi) continue;
        if (slot++ < skip) continue;

        const uint32_t onset = t.onsetMs[i];
        const uint32_t hold = t.holdMs[i];

        RollBlock b;
        b.x0 = bx0;
        b.x1 = bx1;
        b.y = rollBlockY(semi, span, g);
        if (elapsedMs < onset) {
            b.state = RollState::Future;
        } else if (elapsedMs - onset < hold) {
            b.state = RollState::Now;
        } else {
            b.state = RollState::Past;
        }

        out[n++] = b;
    }

    return n;
}

float waveCyclesOnScreen(float freq)
{
    const int semi = semitoneOfFreq(freq);
    if (semi == kNoSemi) return kWaveMinCycles;

    const float t = static_cast<float>(semi - kBarLowSemi) / static_cast<float>(kBarSemiSpan);
    const float c = kWaveMinCycles + t * (kWaveMaxCycles - kWaveMinCycles);

    if (c < kWaveMinCycles) return kWaveMinCycles;
    if (c > kWaveMaxCycles) return kWaveMaxCycles;
    return c;
}

float waveAmplitude(uint32_t sinceOnsetMs, uint32_t holdMs)
{
    if (holdMs == 0) return kWaveTailAmp;

    const float t = (sinceOnsetMs >= holdMs)
                        ? 1.0f
                        : static_cast<float>(sinceOnsetMs) / static_cast<float>(holdMs);

    return 1.0f + t * (kWaveTailAmp - 1.0f);
}

float wavePhase(uint32_t elapsedMs)
{
    // 先整数取模再归一：elapsedMs 再大也不丢精度，而且严格周期
    const float t = static_cast<float>(elapsedMs % kWavePhasePeriodMs) /
                    static_cast<float>(kWavePhasePeriodMs);
    return t * 6.2831853f;
}

int waveY(int x, int screenW, float cycles, float phase, float ampPx, int midY)
{
    if (screenW <= 0) return midY;  // 不除零

    const float a =
        6.2831853f * cycles * static_cast<float>(x) / static_cast<float>(screenW) + phase;
    return midY - static_cast<int>(std::lround(ampPx * std::sin(a)));
}

float beatPhase(uint32_t elapsedMs, int bpm)
{
    const int useBpm = (bpm > 0) ? bpm : 120;

    // 和 buildTimeline 用同一个 float 毫秒/拍，拍点才会和实际发声对齐
    const float msPerBeat = 60000.0f / static_cast<float>(useBpm);
    const float pos = std::fmod(static_cast<float>(elapsedMs), msPerBeat);

    return pos / msPerBeat;
}

int beatLevel(float phase)
{
    if (phase < 1.0f / 3.0f) return 0;  // 负数也落这里：拍首
    if (phase < 2.0f / 3.0f) return 1;
    return 2;
}

NoteGlyph noteGlyphAt(const music::Score &s, int index)
{
    NoteGlyph g;
    if (index < 0 || static_cast<size_t>(index) >= s.notes.size()) return g;

    const music::Note &n = s.notes[index];
    g.valid = true;
    g.digit = static_cast<char>('0' + ((n.step <= 7) ? n.step : 0));
    g.octave = (n.step == 0) ? 0 : n.octave;  // 休止符不画八度点

    return g;
}

uint32_t resumeStartMs(uint32_t nowMs, uint32_t pausedElapsedMs)
{
    return nowMs - pausedElapsedMs;
}

uint32_t remainingHoldMs(uint32_t elapsedMs, uint32_t onsetMs, uint32_t holdMs)
{
    if (elapsedMs <= onsetMs) return holdMs;  // 还没进这个音：整段都还在

    const uint32_t gone = elapsedMs - onsetMs;
    if (gone >= holdMs) return 0;  // 落在静音间隔里 / 已过这个音

    return holdMs - gone;
}

ResumePoint resumePointAt(const music::Timeline &t, uint32_t elapsedMs)
{
    ResumePoint rp;

    rp.index = music::indexAt(t, elapsedMs);
    if (rp.index < 0) return rp;  // 已过曲末 / 空谱

    const size_t i = static_cast<size_t>(rp.index);
    if (i >= t.onsetMs.size() || i >= t.holdMs.size()) {  // 理论上不会发生
        rp.index = -1;
        return rp;
    }

    rp.restMs = remainingHoldMs(elapsedMs, t.onsetMs[i], t.holdMs[i]);
    return rp;
}

bool shouldStopAt(const ResumePoint &at, size_t noteCount)
{
    return at.index < 0 || static_cast<size_t>(at.index) >= noteCount;
}

}  // namespace vizmodel
