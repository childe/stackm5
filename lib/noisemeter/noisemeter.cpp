#include "noisemeter.h"

#include <cmath>

namespace noisemeter {

float meanSquare(const int16_t *samples, size_t n)
{
    if (n == 0) return 0.0f;

    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) sum += samples[i];
    const double mean = sum / static_cast<double>(n);

    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double v = static_cast<double>(samples[i]) - mean;
        acc += v * v;
    }
    constexpr double kFull = 32768.0;
    return static_cast<float>(acc / static_cast<double>(n) / (kFull * kFull));
}

float toDb(float meanSquare)
{
    if (!(meanSquare > 0.0f)) return kFloorDb;
    const float db = 10.0f * std::log10(meanSquare);
    return db < kFloorDb ? kFloorDb : db;
}

// ── Level ───────────────────────────────────────────────────

void Level::push(float meanSquare, float blockMs)
{
    if (!_primed) {
        // 第一块直接取值：从 0 开始平滑的话，进页面后前半秒会显示一个假的「很安静」
        _ms = meanSquare;
        _primed = true;
        return;
    }
    const float alpha = 1.0f - std::exp(-blockMs / _tauMs);
    _ms += alpha * (meanSquare - _ms);
}

float Level::db() const
{
    return toDb(_ms);
}

void Level::reset()
{
    _ms = 0.0f;
    _primed = false;
}

// ── PeakHold ────────────────────────────────────────────────

void PeakHold::update(float db, uint32_t tMs)
{
    if (db >= _db) {
        _db = db;
        _heldAtMs = tMs;
    } else if (tMs - _heldAtMs > _holdMs) {
        // 保持期过了：按实际经过的时间往下落，但不低于当前值
        const float dt = static_cast<float>(tMs - _lastMs) / 1000.0f;
        _db -= _dropDbPerSec * dt;
        if (_db < db) _db = db;
    }
    _lastMs = tMs;
}

void PeakHold::reset()
{
    _db = kFloorDb;
    _heldAtMs = 0;
    _lastMs = 0;
}

// ── History ─────────────────────────────────────────────────

void History::push(float db, uint32_t tMs)
{
    if (db > _max) _max = db;

    if (_count == 0) {
        _head = 0;
        _bins[0] = db;
        _count = 1;
        _binStartMs = tMs;
        return;
    }

    // 跨过了几个箱。中间没数据的箱填地板值（正常喂数据时不会发生，
    // 只防录音断了一阵又接上）。最多补一整屏，再多也看不见
    uint32_t steps = (tMs - _binStartMs) / _binMs;
    if (steps == 0) {
        if (db > _bins[_head]) _bins[_head] = db;
        return;
    }
    if (steps > static_cast<uint32_t>(kCapacity)) steps = kCapacity;

    for (uint32_t s = 0; s < steps; ++s) {
        _head = (_head + 1) % kCapacity;
        _bins[_head] = kFloorDb;
        if (_count < kCapacity) ++_count;
    }
    _bins[_head] = db;
    _binStartMs += ((tMs - _binStartMs) / _binMs) * _binMs;
}

void History::reset()
{
    _head = 0;
    _count = 0;
    _binStartMs = 0;
    _max = kFloorDb;
}

int History::size() const
{
    return _count;
}

float History::at(int i) const
{
    if (i < 0 || i >= _count) return kFloorDb;
    const int oldest = (_head - (_count - 1) + kCapacity) % kCapacity;
    return _bins[(oldest + i) % kCapacity];
}

float History::lastComplete() const
{
    if (_count == 0) return kFloorDb;
    if (_count == 1) return _bins[_head];
    return _bins[(_head - 1 + kCapacity) % kCapacity];
}

// ── Alarm ───────────────────────────────────────────────────

bool Alarm::update(float db, float threshold, uint32_t tMs)
{
    if (db >= threshold) {
        _active = true;
        _lastOverMs = tMs;
    } else if (_active && tMs - _lastOverMs >= _holdMs) {
        _active = false;
    }
    return _active;
}

void Alarm::reset()
{
    _active = false;
    _lastOverMs = 0;
}

// ── 显示 ────────────────────────────────────────────────────

const char *label(float db)
{
    // 大致对应：图书馆 / 办公室、正常说话 / 大声说话、嘈杂餐厅 / 地铁、电钻
    if (db < 45.0f) return "quiet";
    if (db < 65.0f) return "normal";
    if (db < 80.0f) return "loud";
    return "very loud";
}

float fraction(float db, float lo, float hi)
{
    if (!(hi > lo)) return 0.0f;
    const float f = (db - lo) / (hi - lo);
    if (f < 0.0f) return 0.0f;
    if (f > 1.0f) return 1.0f;
    return f;
}

}  // namespace noisemeter
