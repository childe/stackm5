#include "rgb565.h"

#include <cmath>

namespace {

// 先展开到 8 位再插值、再截回 565。这样黑白之间取 v/255 正好得到 v，
// 截回时和 color565 一样直接丢低位，classic 主题的灰阶和原来逐位相同
int lerp8(int a, int b, float t)
{
    return static_cast<int>(std::lround(a + (b - a) * t));
}

}  // namespace

namespace rgb565 {

uint16_t mix(uint16_t a, uint16_t b, float t)
{
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    const int r = lerp8((ar << 3) | (ar >> 2), (br << 3) | (br >> 2), t) >> 3;
    const int g = lerp8((ag << 2) | (ag >> 4), (bg << 2) | (bg >> 4), t) >> 2;
    const int bl = lerp8((ab << 3) | (ab >> 2), (bb << 3) | (bb >> 2), t) >> 3;
    return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}

uint16_t level(uint16_t a, uint16_t b, int v)
{
    return mix(a, b, static_cast<float>(v) / 255.0f);
}

}  // namespace rgb565
