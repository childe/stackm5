#include <unity.h>

#include <string>

#include "hdrlayout.h"

using hdrlayout::BtIcon;

void setUp(void)
{
}

void tearDown(void)
{
}

void test_fit_noise_alarm(void)
{
    hdrlayout::Fit f = hdrlayout::fit("NOISE ALARM", "max 62 alarm 70");
    TEST_ASSERT_EQUAL_STRING("NOIS~", f.title.c_str());
    TEST_ASSERT_EQUAL_STRING("max 62 alarm 70", f.info.c_str());
}

void test_fit_viz_paused(void)
{
    hdrlayout::Fit f = hdrlayout::fit("01 Twinkle Star", "II 12:34/12:34");
    TEST_ASSERT_EQUAL_STRING("01 Tw~", f.title.c_str());
    TEST_ASSERT_EQUAL_STRING("II 12:34/12:34", f.info.c_str());
}

void test_fit_editor(void)
{
    hdrlayout::Fit f = hdrlayout::fit("PLAYING", "1=C 4/4 120");
    TEST_ASSERT_EQUAL_STRING("PLAYING", f.title.c_str());
    TEST_ASSERT_EQUAL_STRING("1=C 4/4 120", f.info.c_str());
}

void test_fit_agents(void)
{
    hdrlayout::Fit f = hdrlayout::fit("AGENTS", "no Mac");
    TEST_ASSERT_EQUAL_STRING("AGENTS", f.title.c_str());
    TEST_ASSERT_EQUAL_STRING("no Mac", f.info.c_str());
}

void test_fit_empty_info_uses_all_cols(void)
{
    hdrlayout::Fit f = hdrlayout::fit(std::string(21, 'A'), "");
    TEST_ASSERT_EQUAL_STRING(std::string(21, 'A').c_str(), f.title.c_str());
    TEST_ASSERT_EQUAL_STRING("", f.info.c_str());

    f = hdrlayout::fit(std::string(22, 'A'), "");
    TEST_ASSERT_EQUAL_STRING((std::string(20, 'A') + "~").c_str(), f.title.c_str());
}

void test_fit_info_too_long(void)
{
    hdrlayout::Fit f = hdrlayout::fit("TITLE", std::string(22, 'x'));
    TEST_ASSERT_EQUAL_STRING((std::string(20, 'x') + "~").c_str(), f.info.c_str());
    TEST_ASSERT_EQUAL_STRING("", f.title.c_str());

    f = hdrlayout::fit("TITLE", std::string(21, 'x'));
    TEST_ASSERT_EQUAL_STRING(std::string(21, 'x').c_str(), f.info.c_str());
    TEST_ASSERT_EQUAL_STRING("", f.title.c_str());
}

void test_fit_n_less_than_2_hides_title(void)
{
    hdrlayout::Fit f = hdrlayout::fit("AB", std::string(19, 'i'));
    TEST_ASSERT_EQUAL_STRING("", f.title.c_str());

    f = hdrlayout::fit("ABC", std::string(18, 'i'));
    TEST_ASSERT_EQUAL_STRING("A~", f.title.c_str());
}

void test_battery_digits(void)
{
    TEST_ASSERT_EQUAL_STRING("--", hdrlayout::batteryDigits(-1).c_str());
    TEST_ASSERT_EQUAL_STRING(" 0", hdrlayout::batteryDigits(0).c_str());
    TEST_ASSERT_EQUAL_STRING(" 7", hdrlayout::batteryDigits(7).c_str());
    TEST_ASSERT_EQUAL_STRING("87", hdrlayout::batteryDigits(87).c_str());
    TEST_ASSERT_EQUAL_STRING("99", hdrlayout::batteryDigits(100).c_str());
    TEST_ASSERT_EQUAL_STRING("99", hdrlayout::batteryDigits(150).c_str());
}

void test_battery_fill(void)
{
    TEST_ASSERT_EQUAL_INT(0, hdrlayout::batteryFill(0, 12));
    TEST_ASSERT_EQUAL_INT(6, hdrlayout::batteryFill(50, 12));
    TEST_ASSERT_EQUAL_INT(12, hdrlayout::batteryFill(100, 12));
    TEST_ASSERT_EQUAL_INT(0, hdrlayout::batteryFill(-1, 12));
    TEST_ASSERT_EQUAL_INT(12, hdrlayout::batteryFill(150, 12));
}

void test_bt_icon(void)
{
    TEST_ASSERT_TRUE(BtIcon::Off == hdrlayout::btIcon(false, true, true, 0));
    TEST_ASSERT_TRUE(BtIcon::Bright == hdrlayout::btIcon(true, true, true, 0));
    TEST_ASSERT_TRUE(BtIcon::Bright == hdrlayout::btIcon(true, true, false, 499));
    TEST_ASSERT_TRUE(BtIcon::Dim == hdrlayout::btIcon(true, true, true, 500));
    TEST_ASSERT_TRUE(BtIcon::Bright == hdrlayout::btIcon(true, true, false, 1000));
    TEST_ASSERT_TRUE(BtIcon::Bright == hdrlayout::btIcon(true, false, true, 500));
    TEST_ASSERT_TRUE(BtIcon::Dim == hdrlayout::btIcon(true, false, false, 0));
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_fit_noise_alarm);
    RUN_TEST(test_fit_viz_paused);
    RUN_TEST(test_fit_editor);
    RUN_TEST(test_fit_agents);
    RUN_TEST(test_fit_empty_info_uses_all_cols);
    RUN_TEST(test_fit_info_too_long);
    RUN_TEST(test_fit_n_less_than_2_hides_title);
    RUN_TEST(test_battery_digits);
    RUN_TEST(test_battery_fill);
    RUN_TEST(test_bt_icon);
    return UNITY_END();
}
