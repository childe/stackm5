#include <unity.h>

#include "dice.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_roll_is_always_between_one_and_six(void)
{
    for (uint32_t value = 0; value < 1000; ++value) {
        const uint8_t result = dice::roll(value * 7919u);
        TEST_ASSERT_TRUE(result >= 1 && result <= 6);
    }
}

void test_shake_triggers_once_then_requires_rearm(void)
{
    dice::ShakeDetector detector;

    TEST_ASSERT_FALSE(detector.update(0.0f, 0.0f, 1.0f, 0));
    TEST_ASSERT_TRUE(detector.update(1.8f, 0.0f, 1.0f, 10));
    TEST_ASSERT_FALSE(detector.update(1.8f, 0.0f, 1.0f, 600));

    TEST_ASSERT_FALSE(detector.update(0.0f, 0.0f, 1.0f, 700));
    TEST_ASSERT_TRUE(detector.update(0.0f, 0.0f, 2.0f, 800));
}

void test_roll_gap_blocks_rapid_rearmed_shakes(void)
{
    dice::ShakeDetector detector;

    TEST_ASSERT_TRUE(detector.update(2.0f, 0.0f, 0.0f, 100));
    detector.update(0.0f, 0.0f, 1.0f, 150);
    TEST_ASSERT_FALSE(detector.update(2.0f, 0.0f, 0.0f, 200));
    TEST_ASSERT_TRUE(detector.update(2.0f, 0.0f, 0.0f, 600));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_roll_is_always_between_one_and_six);
    RUN_TEST(test_shake_triggers_once_then_requires_rearm);
    RUN_TEST(test_roll_gap_blocks_rapid_rearmed_shakes);
    return UNITY_END();
}
