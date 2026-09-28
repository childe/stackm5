// 摇骰子的纯逻辑：设备层提供加速度值和随机熵，UI 层负责绘制。
#pragma once

#include <cstdint>

namespace dice {

// 将任意随机熵映射为 1..6。
uint8_t roll(uint32_t entropy);

class ShakeDetector {
   public:
    // 静止时加速度模长约为 1g；偏离该值超过阈值时认为发生有效摇动。
    bool update(float ax, float ay, float az, uint32_t nowMs);
    void reset();

   private:
    bool _armed = true;
    bool _hasRolled = false;
    uint32_t _lastRollMs = 0;
};

}  // namespace dice
