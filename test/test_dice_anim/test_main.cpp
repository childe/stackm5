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

// ── 出手 ────────────────────────────────────────────────────

void test_settles_on_final_face_at_duration(void)
{
    for (uint8_t face = 1; face <= 6; ++face) {
        for (uint32_t seed : kSeeds) {
            const dice::TumbleFrame f = dice::tumbleAt(face, seed, dice::kTumbleMs);
            TEST_ASSERT_TRUE(f.settled);
            TEST_ASSERT_EQUAL_UINT8(face, f.from);
            TEST_ASSERT_EQUAL_UINT8(face, f.to);
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
                TEST_ASSERT_TRUE(f.flipIndex < dice::kFlipCount);
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
            TEST_ASSERT_TRUE(f.turn > 0.99f);
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

// 实机反馈「翻滚动画没有」：老版本第一次翻面只有 34ms，30fps 下就一帧，
// 看起来是闪一下。每次翻面至少 90ms（30fps 下 3 帧），且一次比一次慢
void test_each_flip_is_slow_enough_to_see(void)
{
    for (int i = 0; i < dice::kFlipCount; ++i) {
        TEST_ASSERT_TRUE(dice::kFlipMs[i] >= 90);
        if (i > 0) TEST_ASSERT_TRUE(dice::kFlipMs[i] > dice::kFlipMs[i - 1]);
    }
}

void test_same_input_same_frame(void)
{
    const dice::TumbleFrame a = dice::tumbleAt(4, 1234u, 321);
    const dice::TumbleFrame b = dice::tumbleAt(4, 1234u, 321);
    TEST_ASSERT_EQUAL_UINT8(a.from, b.from);
    TEST_ASSERT_EQUAL_UINT8(a.to, b.to);
    TEST_ASSERT_EQUAL_FLOAT(a.turn, b.turn);
}

// 不同种子翻出来的过程不一样（否则两颗骰子会一模一样地翻）
void test_different_seeds_tumble_differently(void)
{
    const dice::TumbleFrame a = dice::tumbleAt(6, 1u, 0);
    const dice::TumbleFrame b = dice::tumbleAt(6, 2u, 0);
    const dice::TumbleFrame c = dice::tumbleAt(6, 3u, 0);
    TEST_ASSERT_FALSE(a.from == b.from && b.from == c.from);
}

void test_stagger_and_duration(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, dice::staggerFor(0));
    TEST_ASSERT_EQUAL_UINT32(dice::kStaggerMs, dice::staggerFor(1));
    TEST_ASSERT_EQUAL_UINT32(2 * dice::kStaggerMs, dice::staggerFor(2));
    TEST_ASSERT_EQUAL_UINT32(dice::kTumbleMs, dice::rollDurationMs(1));
    TEST_ASSERT_EQUAL_UINT32(dice::kTumbleMs + 2 * dice::kStaggerMs, dice::rollDurationMs(3));
}

// ── 摇晃 ────────────────────────────────────────────────────

void test_rattle_changes_face_every_step(void)
{
    for (uint32_t seed : kSeeds) {
        uint8_t last = 0;
        for (uint32_t step = 0; step < 200; ++step) {
            const dice::RattleFrame f = dice::rattleAt(seed, step * dice::kRattleStepMs);
            TEST_ASSERT_EQUAL_UINT32(step, f.step);
            TEST_ASSERT_TRUE(f.face >= 1 && f.face <= 6);
            TEST_ASSERT_NOT_EQUAL(last, f.face);
            TEST_ASSERT_TRUE(f.dx >= -dice::kRattleJitterPx && f.dx <= dice::kRattleJitterPx);
            TEST_ASSERT_TRUE(f.dy >= -dice::kRattleJitterPx && f.dy <= dice::kRattleJitterPx);
            last = f.face;
        }
    }
}

// 六个面都会出现（不是只在两三个面之间来回跳）
void test_rattle_shows_all_faces(void)
{
    bool seen[7] = {false};
    for (uint32_t step = 0; step < 200; ++step) {
        seen[dice::rattleAt(77u, step * dice::kRattleStepMs).face] = true;
    }
    for (int face = 1; face <= 6; ++face) TEST_ASSERT_TRUE(seen[face]);
}

void test_rattle_holds_within_a_step(void)
{
    const dice::RattleFrame a = dice::rattleAt(3u, 140);
    const dice::RattleFrame b = dice::rattleAt(3u, 140 + dice::kRattleStepMs - 1);
    TEST_ASSERT_EQUAL_UINT32(a.step, b.step);
    TEST_ASSERT_EQUAL_UINT8(a.face, b.face);
    TEST_ASSERT_EQUAL_INT8(a.dx, b.dx);
}

// ── 出手判定 ────────────────────────────────────────────────

void test_gate_idle_never_throws(void)
{
    dice::ThrowGate gate;
    TEST_ASSERT_FALSE(gate.shaking());
    TEST_ASSERT_FALSE(gate.update(0.0f, 0));
    TEST_ASSERT_FALSE(gate.update(0.0f, 10000));
}

void test_gate_throws_once_after_hand_goes_still(void)
{
    dice::ThrowGate gate;
    gate.shake(1000);
    TEST_ASSERT_TRUE(gate.shaking());

    TEST_ASSERT_FALSE(gate.update(0.05f, 1100));
    TEST_ASSERT_FALSE(gate.update(0.05f, 1000 + dice::ThrowGate::kStillMs - 1));
    TEST_ASSERT_TRUE(gate.update(0.05f, 1000 + dice::ThrowGate::kStillMs));
    TEST_ASSERT_FALSE(gate.shaking());

    // 只出手一次
    TEST_ASSERT_FALSE(gate.update(0.05f, 5000));
}

// 还在晃（偏离没降下来）就一直等，晃停了再从头数 kStillMs
void test_gate_waits_while_hand_keeps_moving(void)
{
    dice::ThrowGate gate;
    gate.shake(0);
    for (uint32_t t = 10; t <= 2000; t += 10) {
        TEST_ASSERT_FALSE(gate.update(0.6f, t));
    }
    TEST_ASSERT_FALSE(gate.update(0.05f, 2000 + dice::ThrowGate::kStillMs - 10));
    TEST_ASSERT_TRUE(gate.update(0.05f, 2000 + dice::ThrowGate::kStillMs));
}

void test_gate_reset_cancels(void)
{
    dice::ThrowGate gate;
    gate.shake(0);
    gate.reset();
    TEST_ASSERT_FALSE(gate.shaking());
    TEST_ASSERT_FALSE(gate.update(0.0f, 10000));
}

// ── 点位 ────────────────────────────────────────────────────

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
    RUN_TEST(test_each_flip_is_slow_enough_to_see);
    RUN_TEST(test_same_input_same_frame);
    RUN_TEST(test_different_seeds_tumble_differently);
    RUN_TEST(test_stagger_and_duration);
    RUN_TEST(test_rattle_changes_face_every_step);
    RUN_TEST(test_rattle_shows_all_faces);
    RUN_TEST(test_rattle_holds_within_a_step);
    RUN_TEST(test_gate_idle_never_throws);
    RUN_TEST(test_gate_throws_once_after_hand_goes_still);
    RUN_TEST(test_gate_waits_while_hand_keeps_moving);
    RUN_TEST(test_gate_reset_cancels);
    RUN_TEST(test_pip_mask_counts_match_face);
    RUN_TEST(test_pip_masks_are_point_symmetric);
    return UNITY_END();
}
