// 番茄钟状态机 —— 纯 C++，不依赖 Arduino；设备 UI 只负责喂时间和画状态。
#pragma once

#include <cstdint>

namespace pomodoro {

enum class Phase { Focus, Break };

class Timer {
   public:
    Timer(uint32_t focusSeconds = 25 * 60, uint32_t breakSeconds = 5 * 60);

    Phase phase() const { return _phase; }
    bool isRunning() const { return _running; }
    uint32_t completedFocuses() const { return _completedFocuses; }

    // 当前显示的剩余整秒。nowMs 使用 Arduino millis()，无符号回绕安全。
    uint32_t remainingSeconds(uint32_t nowMs) const;

    void start(uint32_t nowMs);
    void pause(uint32_t nowMs);
    void toggle(uint32_t nowMs);
    void reset();

    // 切换到下一阶段但不计作完成的专注周期。
    void skip();

    // 到时切换阶段并暂停下一阶段；返回 true 表示发生了切换。
    bool tick(uint32_t nowMs);

   private:
    uint32_t phaseDuration() const;
    void advance(bool completedFocus);

    uint32_t _focusSeconds;
    uint32_t _breakSeconds;
    uint32_t _remainingSeconds;
    uint32_t _startedAtMs = 0;
    uint32_t _completedFocuses = 0;
    Phase _phase = Phase::Focus;
    bool _running = false;
};

}  // namespace pomodoro
