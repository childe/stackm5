#include "volume.h"

#include <M5Cardputer.h>
#include <Preferences.h>

#include "debounce.h"

namespace {

// 九档。间隔取得下密上疏：低档位每一步的响度变化比高档位明显得多
// （0~255 是线性系数，而听感是对数的），上端再细分只是浪费档位。
constexpr uint8_t kSteps[] = {0, 24, 48, 80, 112, 148, 184, 220, 255};
constexpr int kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);

// 默认满档 —— 保持之前硬编码 setVolume(255) 的行为。
// 这块小喇叭在低频几乎不出声，默认就该给足（见 timbre.h 的说明）。
int gLevel = kStepCount - 1;

constexpr const char *kNs = "cfg";
constexpr const char *kKey = "vol";
Debounce gSave(1500);

void apply()
{
    M5Cardputer.Speaker.setVolume(kSteps[gLevel]);
}

}  // namespace

void volume::begin()
{
    Preferences prefs;
    if (prefs.begin(kNs, true)) {
        int v = prefs.getUChar(kKey, kStepCount - 1);
        if (v < 0 || v > kStepCount - 1) v = kStepCount - 1;
        gLevel = v;
        prefs.end();
    }
    apply();
}

void volume::poll()
{
    if (!gSave.due(millis())) return;
    Preferences prefs;
    if (prefs.begin(kNs, false)) {
        prefs.putUChar(kKey, static_cast<uint8_t>(gLevel));
        prefs.end();
    }
}

int volume::level()
{
    return gLevel;
}

int volume::levelMax()
{
    return kStepCount - 1;
}

void volume::up()
{
    if (gLevel < kStepCount - 1) {
        ++gLevel;
        apply();
        gSave.touch(millis());
    }
}

void volume::down()
{
    if (gLevel > 0) {
        --gLevel;
        apply();
        gSave.touch(millis());
    }
}
