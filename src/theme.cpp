#include "theme.h"

#include <M5Cardputer.h>
#include <Preferences.h>

#include "debounce.h"
#include "rgb565.h"

namespace {

// 除 classic 外，正文对比度都在 9~14:1（classic 是 21:1），Solarized 例外只有 5.6:1。
// body 取 dim 和 fg 的 45% 混合，faint 取 bg 和 dim 的中点
constexpr theme::Palette kPalettes[] = {
    // name       bg      fg      body    dim     faint   sel     accent  warn    ok      bad     flash   tint
    {"classic", 0x0000, 0xFFFF, 0xD69A, 0x7BEF, 0x4208, 0x10A6, 0x07FF, 0xFDA0, 0x07E0, 0xF800, 0x8800, 0},
    {"soft", 0x1082, 0xDEFB, 0xAD55, 0x7BEF, 0x4A49, 0x2146, 0x7E58, 0xF58C, 0x8E31, 0xE38E, 0x48C3, 90},
    {"gruvbox", 0x1904, 0xEED6, 0xBD52, 0x940E, 0x5A89, 0x39C6, 0x8533, 0xFDE5, 0xBDC4, 0xFA46, 0x58E3, 90},
    {"nord", 0x29A8, 0xDEFD, 0xA578, 0x7C54, 0x52EE, 0x426B, 0x8E1A, 0xEE51, 0xA5F1, 0xBB0D, 0x5946, 110},
    {"mocha", 0x18E5, 0xCEBE, 0xA558, 0x7C33, 0x4A8C, 0x3188, 0x971A, 0xFD90, 0xA714, 0xF455, 0x5927, 70},
    {"solarized", 0x0146, 0x9514, 0x7431, 0x5B6E, 0x2A6A, 0x01A8, 0x2D13, 0xB440, 0x84C0, 0xD985, 0x48C3, 110},
    {"amber", 0x0840, 0xED07, 0xABC5, 0x7AA4, 0x4162, 0x28E1, 0xF691, 0xFCC5, 0xCD88, 0xFAC5, 0x3900, 255},
};
constexpr int kCount = sizeof(kPalettes) / sizeof(kPalettes[0]);

int gIndex = 0;

constexpr const char *kNs = "cfg";
constexpr const char *kKey = "theme";
Debounce gSave(1500);

}  // namespace

void theme::begin()
{
    Preferences prefs;
    if (prefs.begin(kNs, true)) {
        int v = prefs.getUChar(kKey, 0);
        if (v < 0 || v >= kCount) v = 0;
        gIndex = v;
        prefs.end();
    }
}

void theme::poll()
{
    if (!gSave.due(millis())) return;
    Preferences prefs;
    if (prefs.begin(kNs, false)) {
        prefs.putUChar(kKey, static_cast<uint8_t>(gIndex));
        prefs.end();
    }
}

const theme::Palette &theme::current()
{
    return kPalettes[gIndex];
}

uint16_t theme::level(int v)
{
    const Palette &p = kPalettes[gIndex];
    return rgb565::level(p.bg, p.fg, v);
}

void theme::next()
{
    gIndex = (gIndex + 1) % kCount;
    gSave.touch(millis());
}

void theme::prev()
{
    gIndex = (gIndex + kCount - 1) % kCount;
    gSave.touch(millis());
}
