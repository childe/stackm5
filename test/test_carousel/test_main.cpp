#include <unity.h>

#include "carousel.h"

using carousel::Carousel;

void setUp(void)
{
}

void tearDown(void)
{
}

void test_wrap_handles_negative_and_overflow(void)
{
    TEST_ASSERT_EQUAL_INT(5, carousel::wrap(-1, 6));
    TEST_ASSERT_EQUAL_INT(0, carousel::wrap(6, 6));
    TEST_ASSERT_EQUAL_INT(1, carousel::wrap(13, 6));
    TEST_ASSERT_EQUAL_INT(5, carousel::wrap(-13, 6));
    TEST_ASSERT_EQUAL_INT(0, carousel::wrap(3, 0));
}

void test_step_wraps_around_both_ends(void)
{
    Carousel c(6);
    for (int i = 0; i < 6; ++i) c.step(+1, 1000u * i);
    TEST_ASSERT_EQUAL_INT(0, c.selected());

    c.step(-1, 10000);
    TEST_ASSERT_EQUAL_INT(5, c.selected());
}

void test_offset_eases_from_one_to_zero(void)
{
    Carousel c(6);
    c.step(+1, 1000);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, c.offset(1000));
    TEST_ASSERT_TRUE(c.animating(1000));

    float prev = c.offset(1000);
    for (uint32_t t = 1010; t < 1160; t += 10) {
        const float o = c.offset(t);
        TEST_ASSERT_TRUE(o > 0.0f && o < prev);
        prev = o;
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.125f, c.offset(1080));  // (1 - 0.5)^3
    TEST_ASSERT_TRUE(c.animating(1080));

    TEST_ASSERT_EQUAL_FLOAT(0.0f, c.offset(1160));
    TEST_ASSERT_FALSE(c.animating(1160));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, c.offset(5000));
}

void test_left_step_is_mirror_of_right(void)
{
    Carousel c(6);
    c.step(-1, 1000);
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, c.offset(1000));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.125f, c.offset(1080));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, c.offset(1160));
}

// 连按不丢键：selected 每次都走，视觉落后量钳在 kMaxLag
void test_rapid_steps_accumulate_and_clamp(void)
{
    Carousel c(6);
    for (int i = 0; i < 5; ++i) c.step(+1, 2000);
    TEST_ASSERT_EQUAL_INT(5, c.selected());
    TEST_ASSERT_EQUAL_FLOAT(carousel::kMaxLag, c.offset(2000));

    for (int i = 0; i < 5; ++i) c.step(-1, 9000);
    TEST_ASSERT_EQUAL_INT(0, c.selected());
    TEST_ASSERT_EQUAL_FLOAT(-carousel::kMaxLag, c.offset(9000));
}

// 动画中途再按，从当前画面位置接着转
void test_step_mid_animation_continues_from_current_offset(void)
{
    Carousel c(6);
    c.step(+1, 1000);
    c.step(+1, 1080);  // 此时还剩 0.125 格
    TEST_ASSERT_EQUAL_INT(2, c.selected());
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.125f, c.offset(1080));
}

void test_step_zero_does_nothing(void)
{
    Carousel c(6);
    c.step(0, 1000);
    TEST_ASSERT_EQUAL_INT(0, c.selected());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, c.offset(1000));
    TEST_ASSERT_FALSE(c.animating(1000));
}

void test_jump_sets_selection_and_stops_animation(void)
{
    Carousel c(6);
    c.step(+1, 1000);
    c.jump(4);
    TEST_ASSERT_EQUAL_INT(4, c.selected());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, c.offset(1000));
    TEST_ASSERT_FALSE(c.animating(1000));

    c.jump(-1);
    TEST_ASSERT_EQUAL_INT(5, c.selected());
}

void test_zero_count_is_treated_as_one(void)
{
    Carousel c(0);
    TEST_ASSERT_EQUAL_INT(1, c.count());
    c.step(+1, 1000);
    TEST_ASSERT_EQUAL_INT(0, c.selected());
    c.jump(3);
    TEST_ASSERT_EQUAL_INT(0, c.selected());
}

void test_initial_selection(void)
{
    Carousel c(6, 1);
    TEST_ASSERT_EQUAL_INT(1, c.selected());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, c.offset(0));
    TEST_ASSERT_FALSE(c.animating(0));
}

void test_initial_selection_wraps(void)
{
    TEST_ASSERT_EQUAL_INT(1, Carousel(6, 7).selected());
    TEST_ASSERT_EQUAL_INT(5, Carousel(6, -1).selected());
}

// 绕回的邻居要出现在离正中最近的那一侧
void test_slot_pos_picks_nearest_side(void)
{
    TEST_ASSERT_EQUAL_FLOAT(1.0f, carousel::slotPos(0, 5, 0.0f, 6));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, carousel::slotPos(5, 0, 0.0f, 6));
    TEST_ASSERT_EQUAL_FLOAT(3.0f, carousel::slotPos(3, 0, 0.0f, 6));  // 等距取正
    TEST_ASSERT_EQUAL_FLOAT(1.0f, carousel::slotPos(1, 0, 0.0f, 6));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, carousel::slotPos(0, 0, 0.5f, 6));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.4f, carousel::slotPos(5, 0, 1.4f, 6));
}

// 刚右转完：旧当前项还在正中，新当前项在右侧，然后才滑过来
void test_slot_pos_right_after_step(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, carousel::slotPos(0, 1, 1.0f, 6));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, carousel::slotPos(1, 1, 1.0f, 6));
}

void test_slot_geom_center_and_sides(void)
{
    const carousel::SlotGeom mid = carousel::slotGeom(0.0f);
    TEST_ASSERT_EQUAL_INT(120, mid.x);
    TEST_ASSERT_EQUAL_INT(40, mid.size);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, mid.bright);
    TEST_ASSERT_TRUE(mid.visible);

    const carousel::SlotGeom right = carousel::slotGeom(1.0f);
    const carousel::SlotGeom left = carousel::slotGeom(-1.0f);
    TEST_ASSERT_EQUAL_INT(200, right.x);
    TEST_ASSERT_EQUAL_INT(40, left.x);
    TEST_ASSERT_EQUAL_INT(28, right.size);
    TEST_ASSERT_EQUAL_INT(right.size, left.size);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.45f, right.bright);
    TEST_ASSERT_EQUAL_FLOAT(right.bright, left.bright);

    TEST_ASSERT_EQUAL_INT(34, carousel::slotGeom(0.5f).size);
}

void test_slot_geom_visibility(void)
{
    TEST_ASSERT_FALSE(carousel::slotGeom(2.0f).visible);
    TEST_ASSERT_FALSE(carousel::slotGeom(-2.3f).visible);

    const carousel::SlotGeom far = carousel::slotGeom(1.5f);
    TEST_ASSERT_TRUE(far.visible);
    TEST_ASSERT_EQUAL_INT(28, far.size);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.45f, far.bright);
}

void test_dim565_endpoints_and_half(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xF81F, carousel::dim565(0xF81F, 1.0f));
    TEST_ASSERT_EQUAL_HEX16(0x0000, carousel::dim565(0xFFFF, 0.0f));
    TEST_ASSERT_EQUAL_HEX16(0x07E0, carousel::dim565(0x07E0, 1.2f));

    const uint16_t half = carousel::dim565(0xFFFF, 0.5f);
    TEST_ASSERT_EQUAL_INT(16, (half >> 11) & 0x1F);
    TEST_ASSERT_EQUAL_INT(32, (half >> 5) & 0x3F);
    TEST_ASSERT_EQUAL_INT(16, half & 0x1F);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_wrap_handles_negative_and_overflow);
    RUN_TEST(test_step_wraps_around_both_ends);
    RUN_TEST(test_offset_eases_from_one_to_zero);
    RUN_TEST(test_left_step_is_mirror_of_right);
    RUN_TEST(test_rapid_steps_accumulate_and_clamp);
    RUN_TEST(test_step_mid_animation_continues_from_current_offset);
    RUN_TEST(test_step_zero_does_nothing);
    RUN_TEST(test_jump_sets_selection_and_stops_animation);
    RUN_TEST(test_zero_count_is_treated_as_one);
    RUN_TEST(test_initial_selection);
    RUN_TEST(test_initial_selection_wraps);
    RUN_TEST(test_slot_pos_picks_nearest_side);
    RUN_TEST(test_slot_pos_right_after_step);
    RUN_TEST(test_slot_geom_center_and_sides);
    RUN_TEST(test_slot_geom_visibility);
    RUN_TEST(test_dim565_endpoints_and_half);
    return UNITY_END();
}
