#include "dice.h"

#include <cmath>

namespace {

constexpr float kShakeThresholdG = 0.75f;
constexpr float kRearmThresholdG = 0.20f;
constexpr uint32_t kMinRollGapMs = 450;

}  // namespace

namespace dice {

uint8_t roll(uint32_t entropy)
{
    return static_cast<uint8_t>(entropy % 6) + 1;
}

bool ShakeDetector::update(float ax, float ay, float az, uint32_t nowMs)
{
    const float magnitude = std::sqrt(ax * ax + ay * ay + az * az);
    const float deviation = std::abs(magnitude - 1.0f);

    if (deviation < kRearmThresholdG) _armed = true;

    if (_armed && deviation >= kShakeThresholdG &&
        (!_hasRolled || static_cast<uint32_t>(nowMs - _lastRollMs) >= kMinRollGapMs)) {
        _armed = false;
        _hasRolled = true;
        _lastRollMs = nowMs;
        return true;
    }
    return false;
}

void ShakeDetector::reset()
{
    _armed = true;
    _hasRolled = false;
    _lastRollMs = 0;
}

}  // namespace dice
