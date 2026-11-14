#include <unity.h>

#include "debounce.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_eight_presses_one_write(void)
{
    Debounce d(1500);
    int fired = 0;
    for (uint32_t t = 0; t <= 700; t += 100) d.touch(t);
    for (uint32_t t = 700; t < 2200; t += 10) {
        if (d.due(t)) ++fired;
    }
    TEST_ASSERT_EQUAL_INT(0, fired);
    TEST_ASSERT_TRUE(d.due(2200));
    ++fired;
    if (d.due(5000)) ++fired;
    TEST_ASSERT_EQUAL_INT(1, fired);
}

void test_no_touch_never_due(void)
{
    Debounce d(1500);
    TEST_ASSERT_FALSE(d.due(100000));
}

void test_touch_again_after_fire(void)
{
    Debounce d(1500);
    d.touch(0);
    TEST_ASSERT_TRUE(d.due(1500));
    d.touch(3000);
    TEST_ASSERT_FALSE(d.due(4000));
    TEST_ASSERT_TRUE(d.due(4500));
}

void test_wraparound(void)
{
    Debounce d(1500);
    d.touch(0xFFFFFF00u);
    TEST_ASSERT_TRUE(d.due(0x000005DCu));
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_eight_presses_one_write);
    RUN_TEST(test_no_touch_never_due);
    RUN_TEST(test_touch_again_after_fire);
    RUN_TEST(test_wraparound);
    return UNITY_END();
}
