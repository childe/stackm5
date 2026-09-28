#include "pomodoro.h"

namespace pomodoro {

Timer::Timer(uint32_t focusSeconds, uint32_t breakSeconds)
    : _focusSeconds(focusSeconds), _breakSeconds(breakSeconds), _remainingSeconds(focusSeconds)
{
}

uint32_t Timer::phaseDuration() const
{
    return _phase == Phase::Focus ? _focusSeconds : _breakSeconds;
}

uint32_t Timer::remainingSeconds(uint32_t nowMs) const
{
    if (!_running) return _remainingSeconds;

    const uint32_t elapsedSeconds = (nowMs - _startedAtMs) / 1000;
    return elapsedSeconds >= _remainingSeconds ? 0 : _remainingSeconds - elapsedSeconds;
}

void Timer::start(uint32_t nowMs)
{
    if (_running || _remainingSeconds == 0) return;

    _startedAtMs = nowMs;
    _running = true;
}

void Timer::pause(uint32_t nowMs)
{
    if (!_running) return;

    _remainingSeconds = remainingSeconds(nowMs);
    _running = false;
}

void Timer::toggle(uint32_t nowMs)
{
    _running ? pause(nowMs) : start(nowMs);
}

void Timer::reset()
{
    _running = false;
    _remainingSeconds = phaseDuration();
}

void Timer::advance(bool completedFocus)
{
    if (completedFocus) ++_completedFocuses;

    _phase = _phase == Phase::Focus ? Phase::Break : Phase::Focus;
    _remainingSeconds = phaseDuration();
    _running = false;
}

void Timer::skip()
{
    advance(false);
}

bool Timer::tick(uint32_t nowMs)
{
    if (!_running || remainingSeconds(nowMs) != 0) return false;

    advance(_phase == Phase::Focus);
    return true;
}

}  // namespace pomodoro
