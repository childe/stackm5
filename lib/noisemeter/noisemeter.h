// 噪音计的纯逻辑 —— 零硬件依赖，可以在电脑上单元测试。
//
// 设备层每录完一小块 PCM 就喂进来，这里负责：算这一块的能量 → 按声级计的
// 「Fast」时间计权平滑 → 峰值保持 → 按时间分箱画曲线 → 超限报警。
//
// 所有时间都用「音频时间」（按喂进来的块数累加），不用 millis()：
// 录音任务和主循环不同步，拿墙钟算的话，主循环卡一下曲线就会断一截。
//
// 分贝的基准：满幅正弦是 0 dBFS 附近，数越小越安静。屏幕上显示的是
// dBFS + 校准值 —— 麦克风没出厂校准，这个数只能当「相对大小」看，
// 想接近真实分贝就对着手机分贝仪调校准值（见 README）。
#pragma once

#include <cstddef>
#include <cstdint>

namespace noisemeter {

// 算不出声（全零、空块）时给的地板值，也是所有显示的下限
constexpr float kFloorDb = -100.0f;

// 一块 PCM 去掉直流后的均方值（单位：满幅 32768 为 1.0）。
// 去直流：麦克风有偏置，不去的话静音时也会读出一个不小的能量。
// n == 0 时返回 0
float meanSquare(const int16_t *samples, size_t n);

// 均方值 → dBFS。ms <= 0 时给 kFloorDb
float toDb(float meanSquare);

// 声级计的「Fast」时间计权：均方值做时间常数 tau 的指数平滑，再换成 dB。
// 平滑的是能量而不是 dB —— 对 dB 做平均会系统性地偏低
class Level {
   public:
    explicit Level(float tauMs = 125.0f) : _tauMs(tauMs) {}
    // 喂一块：这块的均方值 + 这块有多长（毫秒）
    void push(float meanSquare, float blockMs);
    float db() const;
    void reset();

   private:
    float _tauMs;
    float _ms = 0.0f;
    bool _primed = false;
};

// 峰值保持：新高立刻跟上，保持 holdMs 不动，之后按 dropDbPerSec 往下落，
// 但不会落到当前值下面
class PeakHold {
   public:
    PeakHold(uint32_t holdMs = 1000, float dropDbPerSec = 20.0f)
        : _holdMs(holdMs), _dropDbPerSec(dropDbPerSec)
    {
    }
    void update(float db, uint32_t tMs);
    float db() const { return _db; }
    void reset();

   private:
    uint32_t _holdMs;
    float _dropDbPerSec;
    float _db = kFloorDb;
    uint32_t _heldAtMs = 0;
    uint32_t _lastMs = 0;
};

// 最近一段时间的曲线：按 binMs 分箱，每箱记这段时间里的最大值
// （记平均的话，拍一下手这种短促的声音在曲线上就看不见了）
class History {
   public:
    static constexpr int kCapacity = 240;  // 一屏宽，一列一箱

    explicit History(uint32_t binMs = 250) : _binMs(binMs) {}
    void push(float db, uint32_t tMs);
    void reset();

    // 已经有多少箱（含正在攒的那一箱），最多 kCapacity
    int size() const;
    // 第 i 箱，0 = 最老，size()-1 = 正在攒的那一箱
    float at(int i) const;
    // 最近一个攒满的箱；一个都没攒满时给正在攒的那一箱。大数字显示它：
    // 每 binMs 才变一次，不会像实时值那样跳得看不清
    float lastComplete() const;
    // 自上次 reset() 以来的最大值
    float maxDb() const { return _max; }

   private:
    uint32_t _binMs;
    float _bins[kCapacity] = {};
    int _head = 0;   // 正在攒的那一箱的下标
    int _count = 0;  // 已用的箱数（含正在攒的）
    uint32_t _binStartMs = 0;
    float _max = kFloorDb;
};

// 报警：超过阈值就亮，降下来以后再保持 holdMs 才灭。
// 不加保持的话，声音在阈值上下晃时屏幕会一直闪烁
class Alarm {
   public:
    explicit Alarm(uint32_t holdMs = 1500) : _holdMs(holdMs) {}
    bool update(float db, float threshold, uint32_t tMs);
    bool active() const { return _active; }
    void reset();

   private:
    uint32_t _holdMs;
    bool _active = false;
    uint32_t _lastOverMs = 0;
};

// 显示用的分贝（已加校准值）→ 文字档位
const char *label(float db);

// db 在 [lo, hi] 里的位置，0..1，越界钳制。画音量条和曲线用
float fraction(float db, float lo, float hi);

}  // namespace noisemeter
