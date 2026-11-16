#include <unity.h>

#include "rgb565.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_mix_endpoints_return_inputs(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x1904, rgb565::mix(0x1904, 0xEED6, 0.0f));
    TEST_ASSERT_EQUAL_HEX16(0xEED6, rgb565::mix(0x1904, 0xEED6, 1.0f));
}

void test_mix_clamps_out_of_range(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x1904, rgb565::mix(0x1904, 0xEED6, -0.5f));
    TEST_ASSERT_EQUAL_HEX16(0xEED6, rgb565::mix(0x1904, 0xEED6, 2.0f));
}

void test_mix_black_white_half_is_mid_grey(void)
{
    // 每通道各自取中点：R 31→16，G 63→32，B 31→16
    TEST_ASSERT_EQUAL_HEX16((16 << 11) | (32 << 5) | 16, rgb565::mix(0x0000, 0xFFFF, 0.5f));
}

void test_mix_channels_do_not_bleed(void)
{
    // 纯红到纯蓝：绿通道始终为 0
    const uint16_t m = rgb565::mix(0xF800, 0x001F, 0.5f);
    TEST_ASSERT_EQUAL_INT(0, (m >> 5) & 0x3F);
}

void test_level_maps_0_255_onto_mix(void)
{
    TEST_ASSERT_EQUAL_HEX16(0x0000, rgb565::level(0x0000, 0xFFFF, 0));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, rgb565::level(0x0000, 0xFFFF, 255));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, rgb565::level(0x0000, 0xFFFF, 300));
    TEST_ASSERT_EQUAL_HEX16(0x0000, rgb565::level(0x0000, 0xFFFF, -4));
}

void test_level_on_black_white_matches_color565_grey(void)
{
    // classic 主题下 level() 要和原来的 color565(v, v, v) 一样，换主题前后画面不变
    for (int v = 0; v <= 255; v += 17) {
        const uint16_t expect = static_cast<uint16_t>(((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3));
        TEST_ASSERT_EQUAL_HEX16(expect, rgb565::level(0x0000, 0xFFFF, v));
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_mix_endpoints_return_inputs);
    RUN_TEST(test_mix_clamps_out_of_range);
    RUN_TEST(test_mix_black_white_half_is_mid_grey);
    RUN_TEST(test_mix_channels_do_not_bleed);
    RUN_TEST(test_level_maps_0_255_onto_mix);
    RUN_TEST(test_level_on_black_white_matches_color565_grey);
    return UNITY_END();
}
