#include <unity.h>

#include "dice.h"
#include "dice_anim.h"

void setUp(void)
{
}

void tearDown(void)
{
}

// 用来遍历的几组种子：0、全 1、以及几个随手挑的
static const uint32_t kSeeds[] = {0u, 1u, 0xffffffffu, 0x12345678u, 0xdeadbeefu, 42u};

static int popcount(uint16_t v)
{
    int n = 0;
    for (; v; v &= static_cast<uint16_t>(v - 1)) ++n;
    return n;
}

void test_settles_on_final_face_at_duration(void)
{
    for (uint8_t face = 1; face <= 6; ++face) {
        for (uint32_t seed : kSeeds) {
            const dice::TumbleFrame f = dice::tumbleAt(face, seed, dice::kTumbleMs);
            TEST_ASSERT_TRUE(f.settled);
            TEST_ASSERT_EQUAL_UINT8(face, f.from);
            TEST_ASSERT_EQUAL_UINT8(face, f.to);
            TEST_ASSERT_EQUAL_INT8(0, f.lift);
            TEST_ASSERT_EQUAL_INT8(0, f.shift);
            TEST_ASSERT_EQUAL_UINT8(dice::kFlipCount, f.flipIndex);

            // 停稳以后一直是这个样子
            const dice::TumbleFrame later = dice::tumbleAt(face, seed, dice::kTumbleMs + 5000);
            TEST_ASSERT_TRUE(later.settled);
            TEST_ASSERT_EQUAL_UINT8(face, later.to);
        }
    }
}

void test_not_settled_before_duration(void)
{
    const dice::TumbleFrame f = dice::tumbleAt(3, 7u, dice::kTumbleMs - 1);
    TEST_ASSERT_FALSE(f.settled);
}

void test_every_frame_is_well_formed(void)
{
    for (uint8_t face = 1; face <= 6; ++face) {
        for (uint32_t seed : kSeeds) {
            for (uint32_t t = 0; t < dice::kTumbleMs; t += 3) {
                const dice::TumbleFrame f = dice::tumbleAt(face, seed, t);
                TEST_ASSERT_TRUE(f.from >= 1 && f.from <= 6);
                TEST_ASSERT_TRUE(f.to >= 1 && f.to <= 6);
                TEST_ASSERT_TRUE(f.turn >= 0.0f && f.turn < 1.0f);
                TEST_ASSERT_TRUE(f.lift <= 0 && f.lift >= -18);
                TEST_ASSERT_TRUE(f.shift >= -6 && f.shift <= 6);
                TEST_ASSERT_TRUE(f.flipIndex <= dice::kFlipCount);
            }
        }
    }
}

// 每次翻面都换了点数（不空翻），而且下一次翻面从上一次转来的面开始
void test_flips_change_face_and_chain(void)
{
    for (uint8_t face = 1; face <= 6; ++face) {
        for (uint32_t seed : kSeeds) {
            int lastIndex = -1;
            uint8_t lastTo = 0;
            for (uint32_t t = 0; t < dice::kTumbleMs; ++t) {
                const dice::TumbleFrame f = dice::tumbleAt(face, seed, t);
                TEST_ASSERT_TRUE(f.flipIndex < dice::kFlipCount);  // 没停稳就还在翻
                TEST_ASSERT_NOT_EQUAL(f.from, f.to);
                if (f.flipIndex != lastIndex) {
                    if (lastIndex >= 0) TEST_ASSERT_EQUAL_UINT8(lastTo, f.from);
                    lastIndex = f.flipIndex;
                }
                lastTo = f.to;
            }
        }
    }
}

// 最后一次翻面转向的就是最终点数
void test_last_flip_lands_on_final_face(void)
{
    for (uint8_t face = 1; face <= 6; ++face) {
        for (uint32_t seed : kSeeds) {
            const dice::TumbleFrame f = dice::tumbleAt(face, seed, dice::kTumbleMs - 1);
            TEST_ASSERT_EQUAL_UINT8(dice::kFlipCount - 1, f.flipIndex);
            TEST_ASSERT_EQUAL_UINT8(face, f.to);
            TEST_ASSERT_TRUE(f.turn > 0.99f);  // 几乎完全转到最终面了
        }
    }
}

// 翻面次数只增不减，且每一次都会经过（不跳号）——每经过一次响一声
void test_flip_index_steps_by_one(void)
{
    for (uint32_t seed : kSeeds) {
        int last = 0;
        for (uint32_t t = 0; t <= dice::kTumbleMs; ++t) {
            const int idx = dice::tumbleAt(5, seed, t).flipIndex;
            TEST_ASSERT_TRUE(idx == last || idx == last + 1);
            last = idx;
        }
        TEST_ASSERT_EQUAL_INT(dice::kFlipCount, last);
    }
}

// 先快后慢：最后一次翻面比第一次慢得多
void test_flips_slow_down(void)
{
    uint32_t firstEnd = 0;
    uint32_t lastStart = 0;
    for (uint32_t t = 0; t <= dice::kTumbleMs; ++t) {
        const int idx = dice::tumbleAt(2, 99u, t).flipIndex;
        if (firstEnd == 0 && idx >= 1) firstEnd = t;
        if (lastStart == 0 && idx >= dice::kFlipCount - 1) lastStart = t;
    }
    const uint32_t firstLen = firstEnd;
    const uint32_t lastLen = dice::kTumbleMs - lastStart;
    TEST_ASSERT_TRUE(lastLen > firstLen * 4);
}

void test_same_input_same_frame(void)
{
    const dice::TumbleFrame a = dice::tumbleAt(4, 1234u, 321);
    const dice::TumbleFrame b = dice::tumbleAt(4, 1234u, 321);
    TEST_ASSERT_EQUAL_UINT8(a.from, b.from);
    TEST_ASSERT_EQUAL_UINT8(a.to, b.to);
    TEST_ASSERT_EQUAL_FLOAT(a.turn, b.turn);
    TEST_ASSERT_EQUAL_INT8(a.lift, b.lift);
    TEST_ASSERT_EQUAL_INT8(a.shift, b.shift);
}

// 不同种子翻出来的过程不一样（否则两颗骰子会一模一样地翻）
void test_different_seeds_tumble_differently(void)
{
    const dice::TumbleFrame a = dice::tumbleAt(6, 1u, 0);
    const dice::TumbleFrame b = dice::tumbleAt(6, 2u, 0);
    const dice::TumbleFrame c = dice::tumbleAt(6, 3u, 0);
    const bool allSame = (a.from == b.from && b.from == c.from) && (a.shift == b.shift && b.shift == c.shift);
    TEST_ASSERT_FALSE(allSame);
}

void test_it_bounces(void)
{
    int minLift = 0;
    for (uint32_t t = 0; t < dice::kTumbleMs; t += 5) {
        const int lift = dice::tumbleAt(1, 5u, t).lift;
        if (lift < minLift) minLift = lift;
    }
    TEST_ASSERT_TRUE(minLift <= -10);  // 第一下跳得够高，看得出来
}

void test_stagger_and_duration(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, dice::staggerFor(0));
    TEST_ASSERT_EQUAL_UINT32(dice::kStaggerMs, dice::staggerFor(1));
    TEST_ASSERT_EQUAL_UINT32(2 * dice::kStaggerMs, dice::staggerFor(2));
    TEST_ASSERT_EQUAL_UINT32(dice::kTumbleMs, dice::rollDurationMs(1));
    TEST_ASSERT_EQUAL_UINT32(dice::kTumbleMs + 2 * dice::kStaggerMs, dice::rollDurationMs(3));
}

void test_pip_mask_counts_match_face(void)
{
    for (uint8_t face = 1; face <= 6; ++face) {
        TEST_ASSERT_EQUAL_INT(face, popcount(dice::pipMask(face)));
    }
    TEST_ASSERT_EQUAL_UINT16(0, dice::pipMask(0));
    TEST_ASSERT_EQUAL_UINT16(0, dice::pipMask(7));
}

// 真骰子的点都是中心对称的：格子 i 和 8-i 要么都有点、要么都没有
void test_pip_masks_are_point_symmetric(void)
{
    for (uint8_t face = 1; face <= 6; ++face) {
        const uint16_t m = dice::pipMask(face);
        for (int i = 0; i < 9; ++i) {
            TEST_ASSERT_EQUAL(((m >> i) & 1u), ((m >> (8 - i)) & 1u));
        }
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_settles_on_final_face_at_duration);
    RUN_TEST(test_not_settled_before_duration);
    RUN_TEST(test_every_frame_is_well_formed);
    RUN_TEST(test_flips_change_face_and_chain);
    RUN_TEST(test_last_flip_lands_on_final_face);
    RUN_TEST(test_flip_index_steps_by_one);
    RUN_TEST(test_flips_slow_down);
    RUN_TEST(test_same_input_same_frame);
    RUN_TEST(test_different_seeds_tumble_differently);
    RUN_TEST(test_it_bounces);
    RUN_TEST(test_stagger_and_duration);
    RUN_TEST(test_pip_mask_counts_match_face);
    RUN_TEST(test_pip_masks_are_point_symmetric);
    return UNITY_END();
}
