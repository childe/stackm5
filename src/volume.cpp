#include "volume.h"

#include <M5Cardputer.h>

namespace {

// 九档。间隔取得下密上疏：低档位每一步的响度变化比高档位明显得多
// （0~255 是线性系数，而听感是对数的），上端再细分只是浪费档位。
constexpr uint8_t kSteps[] = {0, 24, 48, 80, 112, 148, 184, 220, 255};
constexpr int kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);

// 默认满档 —— 保持之前硬编码 setVolume(255) 的行为。
// 这块小喇叭在低频几乎不出声，默认就该给足（见 timbre.h 的说明）。
int gLevel = kStepCount - 1;

void apply()
{
    M5Cardputer.Speaker.setVolume(kSteps[gLevel]);
}

}  // namespace

void volume::begin()
{
    apply();
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
    }
}

void volume::down()
{
    if (gLevel > 0) {
        --gLevel;
        apply();
    }
}
