#include <unity.h>

#include <cmath>
#include <vector>

#include "noisemeter.h"

using noisemeter::kFloorDb;

void setUp(void)
{
}

void tearDown(void)
{
}

static std::vector<int16_t> sine(float amplitude, int n, float cyclesPerBlock = 8.0f, int16_t dc = 0)
{
    std::vector<int16_t> v(n);
    for (int i = 0; i < n; ++i) {
        const float x = amplitude * 32767.0f * std::sin(2.0f * 3.14159265f * cyclesPerBlock * i / n);
        v[i] = static_cast<int16_t>(std::lround(x) + dc);
    }
    return v;
}

// ── 能量 / 分贝 ─────────────────────────────────────────────

void test_silence_is_floor(void)
{
    std::vector<int16_t> z(256, 0);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, noisemeter::meanSquare(z.data(), z.size()));
    TEST_ASSERT_EQUAL_FLOAT(kFloorDb, noisemeter::toDb(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, noisemeter::meanSquare(nullptr, 0));
}

// 麦克风有直流偏置：一个常数不是声音，能量必须是 0
void test_dc_offset_is_removed(void)
{
    std::vector<int16_t> dc(256, 1200);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, noisemeter::meanSquare(dc.data(), dc.size()));

    const auto a = sine(0.1f, 256);
    const auto b = sine(0.1f, 256, 8.0f, 1200);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, noisemeter::meanSquare(a.data(), a.size()),
                             noisemeter::meanSquare(b.data(), b.size()));
}

// 满幅正弦的均方是 1/2 → -3 dBFS；幅度减半 → 再低 6 dB
void test_sine_levels(void)
{
    const auto full = sine(1.0f, 256);
    const auto half = sine(0.5f, 256);
    const float dFull = noisemeter::toDb(noisemeter::meanSquare(full.data(), full.size()));
    const float dHalf = noisemeter::toDb(noisemeter::meanSquare(half.data(), half.size()));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -3.01f, dFull);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 6.02f, dFull - dHalf);
}

// ── Level（Fast 计权）───────────────────────────────────────

void test_level_first_block_is_taken_as_is(void)
{
    noisemeter::Level lv;
    lv.push(0.01f, 16.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -20.0f, lv.db());
}

// 一个时间常数后走完约 63%（在能量上）
void test_level_follows_with_time_constant(void)
{
    noisemeter::Level lv(125.0f);
    lv.push(1e-6f, 16.0f);  // 先安静
    for (int i = 0; i < 125; ++i) lv.push(1.0f, 1.0f);  // 125ms 的大声
    const float expected = 1e-6f + (1.0f - 1e-6f) * (1.0f - std::exp(-1.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, noisemeter::toDb(expected), lv.db());
}

void test_level_reset_forgets(void)
{
    noisemeter::Level lv;
    lv.push(1.0f, 16.0f);
    lv.reset();
    TEST_ASSERT_EQUAL_FLOAT(kFloorDb, lv.db());
    lv.push(0.01f, 16.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -20.0f, lv.db());
}

// ── PeakHold ────────────────────────────────────────────────

void test_peak_rises_holds_then_drops(void)
{
    noisemeter::PeakHold p(1000, 20.0f);
    p.update(-40.0f, 0);
    p.update(-10.0f, 100);
    TEST_ASSERT_EQUAL_FLOAT(-10.0f, p.db());

    p.update(-40.0f, 900);  // 还在保持期
    TEST_ASSERT_EQUAL_FLOAT(-10.0f, p.db());

    p.update(-40.0f, 1100);  // 保持期刚过
    p.update(-40.0f, 1600);  // 又过 0.5 秒 → 落 10 dB
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -20.0f, p.db());
}

void test_peak_never_drops_below_current(void)
{
    noisemeter::PeakHold p(0, 1000.0f);
    p.update(-10.0f, 0);
    p.update(-30.0f, 100);
    p.update(-30.0f, 5000);
    TEST_ASSERT_EQUAL_FLOAT(-30.0f, p.db());
}

// ── History ─────────────────────────────────────────────────

void test_history_keeps_max_per_bin(void)
{
    noisemeter::History h(250);
    h.push(-50.0f, 0);
    h.push(-20.0f, 100);  // 拍一下手
    h.push(-50.0f, 200);
    TEST_ASSERT_EQUAL_INT(1, h.size());
    TEST_ASSERT_EQUAL_FLOAT(-20.0f, h.at(0));

    h.push(-45.0f, 250);  // 下一箱
    TEST_ASSERT_EQUAL_INT(2, h.size());
    TEST_ASSERT_EQUAL_FLOAT(-20.0f, h.at(0));
    TEST_ASSERT_EQUAL_FLOAT(-45.0f, h.at(1));
    TEST_ASSERT_EQUAL_FLOAT(-20.0f, h.lastComplete());
}

void test_history_wraps_oldest_first(void)
{
    noisemeter::History h(10);
    const int n = noisemeter::History::kCapacity + 5;
    for (int i = 0; i < n; ++i) h.push(static_cast<float>(-i), static_cast<uint32_t>(i * 10));
    TEST_ASSERT_EQUAL_INT(noisemeter::History::kCapacity, h.size());
    TEST_ASSERT_EQUAL_FLOAT(-5.0f, h.at(0));
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(-(n - 1)), h.at(h.size() - 1));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, h.maxDb());
}

// 录音断了一阵：中间的箱填地板值，不把旧数据错位拼接
void test_history_gap_fills_floor(void)
{
    noisemeter::History h(100);
    h.push(-30.0f, 0);
    h.push(-40.0f, 350);
    TEST_ASSERT_EQUAL_INT(4, h.size());
    TEST_ASSERT_EQUAL_FLOAT(-30.0f, h.at(0));
    TEST_ASSERT_EQUAL_FLOAT(kFloorDb, h.at(1));
    TEST_ASSERT_EQUAL_FLOAT(kFloorDb, h.at(2));
    TEST_ASSERT_EQUAL_FLOAT(-40.0f, h.at(3));
}

void test_history_reset(void)
{
    noisemeter::History h(100);
    h.push(-10.0f, 0);
    h.push(-10.0f, 200);
    h.reset();
    TEST_ASSERT_EQUAL_INT(0, h.size());
    TEST_ASSERT_EQUAL_FLOAT(kFloorDb, h.maxDb());
    TEST_ASSERT_EQUAL_FLOAT(kFloorDb, h.lastComplete());
    TEST_ASSERT_EQUAL_FLOAT(kFloorDb, h.at(0));
}

// ── Alarm ───────────────────────────────────────────────────

void test_alarm_holds_after_level_drops(void)
{
    noisemeter::Alarm a(1500);
    TEST_ASSERT_FALSE(a.update(60.0f, 70.0f, 0));
    TEST_ASSERT_TRUE(a.update(75.0f, 70.0f, 100));
    TEST_ASSERT_TRUE(a.update(60.0f, 70.0f, 1500));  // 降下来了，还在保持
    TEST_ASSERT_FALSE(a.update(60.0f, 70.0f, 1600));
}

// 在阈值上下晃：一直亮着，不闪
void test_alarm_does_not_flicker_around_threshold(void)
{
    noisemeter::Alarm a(1500);
    for (uint32_t t = 0; t < 5000; t += 50) {
        const float db = (t / 50) % 2 ? 71.0f : 69.0f;
        const bool on = a.update(db, 70.0f, t);
        if (t >= 50) TEST_ASSERT_TRUE(on);
    }
}

// ── 显示 ────────────────────────────────────────────────────

void test_labels(void)
{
    TEST_ASSERT_EQUAL_STRING("quiet", noisemeter::label(30.0f));
    TEST_ASSERT_EQUAL_STRING("normal", noisemeter::label(55.0f));
    TEST_ASSERT_EQUAL_STRING("loud", noisemeter::label(70.0f));
    TEST_ASSERT_EQUAL_STRING("very loud", noisemeter::label(95.0f));
}

void test_fraction_clamps(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, noisemeter::fraction(10.0f, 30.0f, 100.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, noisemeter::fraction(120.0f, 30.0f, 100.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, noisemeter::fraction(65.0f, 30.0f, 100.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, noisemeter::fraction(50.0f, 50.0f, 50.0f));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_silence_is_floor);
    RUN_TEST(test_dc_offset_is_removed);
    RUN_TEST(test_sine_levels);
    RUN_TEST(test_level_first_block_is_taken_as_is);
    RUN_TEST(test_level_follows_with_time_constant);
    RUN_TEST(test_level_reset_forgets);
    RUN_TEST(test_peak_rises_holds_then_drops);
    RUN_TEST(test_peak_never_drops_below_current);
    RUN_TEST(test_history_keeps_max_per_bin);
    RUN_TEST(test_history_wraps_oldest_first);
    RUN_TEST(test_history_gap_fills_floor);
    RUN_TEST(test_history_reset);
    RUN_TEST(test_alarm_holds_after_level_drops);
    RUN_TEST(test_alarm_does_not_flicker_around_threshold);
    RUN_TEST(test_labels);
    RUN_TEST(test_fraction_clamps);
    return UNITY_END();
}
