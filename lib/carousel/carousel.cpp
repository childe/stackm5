#include "carousel.h"

#include <algorithm>
#include <cmath>

namespace carousel {

int wrap(int i, int n)
{
    if (n <= 0) return 0;
    const int r = i % n;
    return r < 0 ? r + n : r;
}

Carousel::Carousel(int count, int initial)
    : _count(count < 1 ? 1 : count), _sel(wrap(initial, _count))
{
}

void Carousel::step(int dir, uint32_t nowMs)
{
    if (dir == 0) return;
    const int d = dir > 0 ? 1 : -1;
    _sel = wrap(_sel + d, _count);
    // 从当前画面位置接着转，而不是从 ±1 重来，否则连按时画面会往回跳
    _startOffset = std::clamp(offset(nowMs) + static_cast<float>(d), -kMaxLag, kMaxLag);
    _startMs = nowMs;
}

void Carousel::jump(int index)
{
    _sel = wrap(index, _count);
    _startOffset = 0.0f;
}

float Carousel::offset(uint32_t nowMs) const
{
    if (_startOffset == 0.0f) return 0.0f;
    // 无符号减法：millis() 49 天回绕时也对
    const uint32_t elapsed = nowMs - _startMs;
    if (elapsed >= kStepMs) return 0.0f;
    const float rest = 1.0f - static_cast<float>(elapsed) / static_cast<float>(kStepMs);
    return _startOffset * rest * rest * rest;  // ease-out 三次方
}

bool Carousel::animating(uint32_t nowMs) const
{
    return offset(nowMs) != 0.0f;
}

float slotPos(int item, int sel, float offset, int n)
{
    const int d0 = wrap(item - sel, n);
    const int d1 = d0 - n;
    const float p0 = static_cast<float>(d0) + offset;
    const float p1 = static_cast<float>(d1) + offset;
    return std::fabs(p1) < std::fabs(p0) ? p1 : p0;
}

SlotGeom slotGeom(float pos)
{
    const float a = std::fabs(pos);
    SlotGeom g;
    g.x = kCenterX + static_cast<int>(std::lround(kSlotSpacing * pos));
    if (a <= 1.0f) {
        g.size = static_cast<int>(std::lround(kBigIcon - (kBigIcon - kSmallIcon) * a));
        g.bright = 1.0f - (1.0f - kSideBright) * a;
    } else {
        g.size = kSmallIcon;
        g.bright = kSideBright;
    }
    g.visible = a < 2.0f;
    return g;
}

uint16_t dim565(uint16_t c, float k)
{
    if (k <= 0.0f) return 0;
    if (k >= 1.0f) return c;
    const auto scale = [k](int ch) { return static_cast<int>(std::lround(ch * k)); };
    const int r = scale((c >> 11) & 0x1F);
    const int g = scale((c >> 5) & 0x3F);
    const int b = scale(c & 0x1F);
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

}  // namespace carousel
