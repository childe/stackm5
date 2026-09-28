#include <unity.h>

#include <cstdint>

#include "pomodoro.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_defaults_to_stopped_focus(void)
{
    pomodoro::Timer timer(25, 5);

    TEST_ASSERT_EQUAL(pomodoro::Phase::Focus, timer.phase());
    TEST_ASSERT_FALSE(timer.isRunning());
    TEST_ASSERT_EQUAL_UINT32(25, timer.remainingSeconds(0));
    TEST_ASSERT_EQUAL_UINT32(0, timer.completedFocuses());
}

void test_counts_down_only_while_running(void)
{
    pomodoro::Timer timer(25, 5);

    timer.start(1000);
    TEST_ASSERT_TRUE(timer.isRunning());
    TEST_ASSERT_EQUAL_UINT32(25, timer.remainingSeconds(1000));
    TEST_ASSERT_EQUAL_UINT32(24, timer.remainingSeconds(2000));
    TEST_ASSERT_EQUAL_UINT32(1, timer.remainingSeconds(25000));

    timer.pause(25000);
    TEST_ASSERT_FALSE(timer.isRunning());
    TEST_ASSERT_EQUAL_UINT32(1, timer.remainingSeconds(90000));
}

void test_completion_enters_paused_break_and_counts_focus(void)
{
    pomodoro::Timer timer(3, 2);

    timer.start(100);
    TEST_ASSERT_FALSE(timer.tick(3099));
    TEST_ASSERT_TRUE(timer.isRunning());
    TEST_ASSERT_TRUE(timer.tick(3100));

    TEST_ASSERT_EQUAL(pomodoro::Phase::Break, timer.phase());
    TEST_ASSERT_FALSE(timer.isRunning());
    TEST_ASSERT_EQUAL_UINT32(2, timer.remainingSeconds(3100));
    TEST_ASSERT_EQUAL_UINT32(1, timer.completedFocuses());
}

void test_completion_of_break_does_not_increment_focus_count(void)
{
    pomodoro::Timer timer(2, 1);

    timer.start(0);
    TEST_ASSERT_TRUE(timer.tick(2000));
    timer.start(2000);
    TEST_ASSERT_TRUE(timer.tick(3000));

    TEST_ASSERT_EQUAL(pomodoro::Phase::Focus, timer.phase());
    TEST_ASSERT_EQUAL_UINT32(1, timer.completedFocuses());
    TEST_ASSERT_EQUAL_UINT32(2, timer.remainingSeconds(3000));
}

void test_skip_never_counts_as_completed_focus(void)
{
    pomodoro::Timer timer(25, 5);

    timer.start(0);
    timer.skip();
    TEST_ASSERT_EQUAL(pomodoro::Phase::Break, timer.phase());
    TEST_ASSERT_FALSE(timer.isRunning());
    TEST_ASSERT_EQUAL_UINT32(0, timer.completedFocuses());

    timer.skip();
    TEST_ASSERT_EQUAL(pomodoro::Phase::Focus, timer.phase());
    TEST_ASSERT_EQUAL_UINT32(0, timer.completedFocuses());
}

void test_reset_keeps_phase_and_restores_its_full_duration(void)
{
    pomodoro::Timer timer(25, 5);

    timer.start(0);
    timer.skip();
    timer.start(0);
    timer.pause(3000);
    TEST_ASSERT_EQUAL_UINT32(2, timer.remainingSeconds(3000));

    timer.reset();
    TEST_ASSERT_FALSE(timer.isRunning());
    TEST_ASSERT_EQUAL_UINT32(5, timer.remainingSeconds(3000));
}

void test_millis_wraparound_is_safe(void)
{
    pomodoro::Timer timer(5, 2);
    const uint32_t beforeWrap = UINT32_MAX - 1500;

    timer.start(beforeWrap);
    TEST_ASSERT_EQUAL_UINT32(3, timer.remainingSeconds(500));
    TEST_ASSERT_FALSE(timer.tick(500));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_to_stopped_focus);
    RUN_TEST(test_counts_down_only_while_running);
    RUN_TEST(test_completion_enters_paused_break_and_counts_focus);
    RUN_TEST(test_completion_of_break_does_not_increment_focus_count);
    RUN_TEST(test_skip_never_counts_as_completed_focus);
    RUN_TEST(test_reset_keeps_phase_and_restores_its_full_duration);
    RUN_TEST(test_millis_wraparound_is_safe);
    return UNITY_END();
}
