// 低电量判断的纯逻辑 —— 零硬件依赖，可以在电脑上单元测试。
//
// 只看电压，不看百分比：百分比是线性换算的，在 3.7V 平台区会多报约 35 个点，
// 真快没电时它还显示二十多。阈值来自实测：我们的固件在 3.37V 已经起不来。
//
// 读数取最近 6 次的平均（5 秒一次 ≈ 30 秒），放音乐时的瞬间掉压被平均掉。
// 级别只会变差，平均回到 3600mV 以上（插线、开关 ON 在充）才回到 Ok；
// 所以每一级只提醒一次，充上电之后再掉下去才会再提醒。
#pragma once

#include <cstdint>

namespace lowbat {

constexpr int kLowMv = 3500;       // 低于它：提醒一次
constexpr int kCriticalMv = 3400;  // 低于它：紧急提醒一次
constexpr int kRecoverMv = 3600;   // 平均回到它以上：恢复，重新武装
constexpr int kWindow = 6;
constexpr int kMinSamples = 3;

enum class Level : uint8_t { Ok, Low, Critical };
enum class Event : uint8_t { None, BecameLow, BecameCritical };

class Monitor {
   public:
    // 喂一次电压读数（mV）。<= 0 是读失败，忽略。级别变差时返回对应事件
    Event feed(int mv);

    Level level() const { return level_; }
    int avgMv() const;  // 还没有读数时返回 0

   private:
    int buf_[kWindow] = {};
    int count_ = 0;  // 有效读数个数，封顶 kWindow
    int next_ = 0;
    Level level_ = Level::Ok;
};

// 标题行电池图标该不该画红：Low 常红，Critical 500ms 一相闪
bool iconRed(Level level, uint32_t nowMs);

}  // namespace lowbat
