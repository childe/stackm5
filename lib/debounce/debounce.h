// 「静默 N 毫秒后触发一次」—— 零硬件依赖，可以在电脑上单元测试。
//
// 用于延迟写 Flash：连续改动只在最后一次改动后安静 quietMs 才触发一次。
// 时间用 uint32_t 无符号差值比较，millis() 回绕也成立。
#pragma once

#include <cstdint>

class Debounce {
   public:
    explicit Debounce(uint32_t quietMs) : _quietMs(quietMs) {}

    // 记一次变化
    void touch(uint32_t nowMs);

    // 自最后一次 touch 已静默 >= quietMs 且尚未触发过 → true（每次 touch 后只返回一次）
    bool due(uint32_t nowMs);

   private:
    uint32_t _quietMs;
    uint32_t _last = 0;
    bool _pending = false;
};
