#include "lowbat.h"

namespace lowbat {

Event Monitor::feed(int mv)
{
    if (mv <= 0) return Event::None;

    buf_[next_] = mv;
    next_ = (next_ + 1) % kWindow;
    if (count_ < kWindow) ++count_;
    if (count_ < kMinSamples) return Event::None;

    const int avg = avgMv();
    if (avg >= kRecoverMv) {
        level_ = Level::Ok;
        return Event::None;
    }

    const Level now = avg < kCriticalMv ? Level::Critical : (avg < kLowMv ? Level::Low : Level::Ok);
    if (now <= level_) return Event::None;  // 只往差里走，回升一点不算恢复
    level_ = now;
    return now == Level::Critical ? Event::BecameCritical : Event::BecameLow;
}

int Monitor::avgMv() const
{
    if (count_ == 0) return 0;
    int sum = 0;
    for (int i = 0; i < count_; ++i) sum += buf_[i];
    return sum / count_;
}

bool iconRed(Level level, uint32_t nowMs)
{
    switch (level) {
        case Level::Ok:
            return false;
        case Level::Low:
            return true;
        case Level::Critical:
            return (nowMs / 500) % 2 == 0;
    }
    return false;
}

}  // namespace lowbat
