#include <lowbat.h>
#include <unity.h>

using lowbat::Event;
using lowbat::Level;
using lowbat::Monitor;

void setUp(void) {}
void tearDown(void) {}

// 连喂 n 个同样的读数，返回最后一次的事件
static Event feedN(Monitor &m, int mv, int n)
{
    Event e = Event::None;
    for (int i = 0; i < n; ++i) e = m.feed(mv);
    return e;
}

void test_fewer_than_three_samples_judges_nothing(void)
{
    Monitor m;
    TEST_ASSERT_EQUAL(Event::None, m.feed(3300));
    TEST_ASSERT_EQUAL(Event::None, m.feed(3300));
    TEST_ASSERT_EQUAL(Level::Ok, m.level());
    TEST_ASSERT_EQUAL(Event::BecameCritical, m.feed(3300));
}

void test_full_battery_stays_ok(void)
{
    Monitor m;
    TEST_ASSERT_EQUAL(Event::None, feedN(m, 4100, 10));
    TEST_ASSERT_EQUAL(Level::Ok, m.level());
}

void test_below_3500_becomes_low_once(void)
{
    Monitor m;
    feedN(m, 3700, 6);
    Event first = Event::None;
    int fired = 0;
    for (int i = 0; i < 12; ++i) {
        const Event e = m.feed(3450);
        if (e != Event::None) {
            ++fired;
            first = e;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, fired);
    TEST_ASSERT_EQUAL(Event::BecameLow, first);
    TEST_ASSERT_EQUAL(Level::Low, m.level());
}

void test_exactly_3500_is_ok(void)
{
    Monitor m;
    TEST_ASSERT_EQUAL(Event::None, feedN(m, 3500, 6));
    TEST_ASSERT_EQUAL(Level::Ok, m.level());
}

void test_low_then_below_3400_becomes_critical(void)
{
    Monitor m;
    feedN(m, 3450, 6);
    TEST_ASSERT_EQUAL(Level::Low, m.level());
    int critical = 0;
    for (int i = 0; i < 12; ++i) {
        if (m.feed(3350) == Event::BecameCritical) ++critical;
    }
    TEST_ASSERT_EQUAL_INT(1, critical);
    TEST_ASSERT_EQUAL(Level::Critical, m.level());
}

// 放音乐时电压瞬间掉一下：一个低读数被前后的正常读数平均掉，不报
void test_single_dip_is_averaged_away(void)
{
    Monitor m;
    feedN(m, 3650, 6);
    TEST_ASSERT_EQUAL(Event::None, m.feed(3200));
    TEST_ASSERT_EQUAL(Event::None, feedN(m, 3650, 6));
    TEST_ASSERT_EQUAL(Level::Ok, m.level());
}

void test_average_is_over_last_six(void)
{
    Monitor m;
    feedN(m, 4000, 6);
    feedN(m, 3600, 6);
    TEST_ASSERT_EQUAL_INT(3600, m.avgMv());
}

// 插线充电读数回升一点，没过 3600 不算恢复，也不会重复提醒
void test_small_rise_does_not_reset(void)
{
    Monitor m;
    feedN(m, 3450, 6);
    feedN(m, 3550, 6);
    TEST_ASSERT_EQUAL(Level::Low, m.level());
    TEST_ASSERT_EQUAL(Event::None, feedN(m, 3450, 6));
}

// 平均回到 3600 以上算充上电了，之后再掉下去重新提醒
void test_recovery_rearms(void)
{
    Monitor m;
    feedN(m, 3450, 6);
    feedN(m, 3700, 6);
    TEST_ASSERT_EQUAL(Level::Ok, m.level());
    int low = 0;
    for (int i = 0; i < 6; ++i) {
        if (m.feed(3450) == Event::BecameLow) ++low;
    }
    TEST_ASSERT_EQUAL_INT(1, low);
}

void test_invalid_readings_ignored(void)
{
    Monitor m;
    feedN(m, 3700, 6);
    TEST_ASSERT_EQUAL(Event::None, feedN(m, 0, 6));
    TEST_ASSERT_EQUAL(Event::None, feedN(m, -1, 6));
    TEST_ASSERT_EQUAL_INT(3700, m.avgMv());
}

void test_icon_red_by_level(void)
{
    TEST_ASSERT_FALSE(lowbat::iconRed(Level::Ok, 0));
    TEST_ASSERT_TRUE(lowbat::iconRed(Level::Low, 0));
    TEST_ASSERT_TRUE(lowbat::iconRed(Level::Low, 700));
    // 紧急：500ms 一相闪
    TEST_ASSERT_TRUE(lowbat::iconRed(Level::Critical, 0));
    TEST_ASSERT_FALSE(lowbat::iconRed(Level::Critical, 500));
    TEST_ASSERT_TRUE(lowbat::iconRed(Level::Critical, 1000));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_fewer_than_three_samples_judges_nothing);
    RUN_TEST(test_full_battery_stays_ok);
    RUN_TEST(test_below_3500_becomes_low_once);
    RUN_TEST(test_exactly_3500_is_ok);
    RUN_TEST(test_low_then_below_3400_becomes_critical);
    RUN_TEST(test_single_dip_is_averaged_away);
    RUN_TEST(test_average_is_over_last_six);
    RUN_TEST(test_small_rise_does_not_reset);
    RUN_TEST(test_recovery_rearms);
    RUN_TEST(test_invalid_readings_ignored);
    RUN_TEST(test_icon_red_by_level);
    return UNITY_END();
}
