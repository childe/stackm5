#include "vizmodel.h"

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

}  // namespace vizmodel
