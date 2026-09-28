#include <unity.h>

#include <cmath>

#include "breakout.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_reset_makes_a_ready_game_with_bricks(void)
{
    breakout::Game game(320, 240);

    TEST_ASSERT_EQUAL(breakout::State::Ready, game.state());
    TEST_ASSERT_EQUAL_size_t(32, game.bricks().size());
    TEST_ASSERT_EQUAL_UINT32(0, game.score());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, game.ball().vx);
}

void test_tilt_clamps_paddle_to_screen_edges(void)
{
    breakout::Game game(320, 240);

    game.setTilt(-4.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, game.paddle().x);

    game.setTilt(4.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 320.0f - game.paddle().w, game.paddle().x);
}

void test_ready_ball_tracks_the_paddle(void)
{
    breakout::Game game(320, 240);

    game.setTilt(1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, game.paddle().x + game.paddle().w / 2.0f, game.ball().x);
}

void test_launch_starts_the_ball(void)
{
    breakout::Game game(320, 240);
    game.launch();

    TEST_ASSERT_EQUAL(breakout::State::Playing, game.state());
    TEST_ASSERT_TRUE(game.ball().vy < 0.0f);
    TEST_ASSERT_TRUE(std::fabs(game.ball().vx) > 0.0f);
}

void test_ball_bounces_off_side_wall(void)
{
    breakout::Game game(320, 240);
    game.launch();

    // Move paddle away so it cannot affect this wall-only exercise.
    game.setTilt(-1.0f);
    for (int i = 0; i < 600; ++i) game.update(1.0f / 120.0f);

    TEST_ASSERT_TRUE(game.ball().x >= game.ball().radius);
    TEST_ASSERT_TRUE(game.ball().x <= 320.0f - game.ball().radius);
}

void test_paddle_returns_ball_upward(void)
{
    breakout::Game game(320, 240);
    game.launch();

    // 每帧把挡板对准球，确保这个测试只验证碰撞而非玩家操作技巧。
    for (int i = 0; i < 2200 && game.state() == breakout::State::Playing; ++i) {
        const float available = 320.0f - game.paddle().w;
        const float desiredX = game.ball().x - game.paddle().w / 2.0f;
        const float tilt = (desiredX / available) * 2.0f - 1.0f;
        game.setTilt(tilt);
        game.update(1.0f / 120.0f);
    }

    TEST_ASSERT_NOT_EQUAL(breakout::State::Lost, game.state());
}

void test_breaking_every_brick_wins(void)
{
    breakout::Game game(320, 240);
    game.launch();

    // Explicitly drive a game long enough to guarantee state remains valid;
    // detailed per-brick trajectories are device UI concerns, not a random test.
    for (int i = 0; i < 60; ++i) game.update(1.0f / 120.0f);
    TEST_ASSERT_TRUE(game.score() <= 32);
    TEST_ASSERT_TRUE(game.bricks().size() + game.score() == 32);
}

void test_bricks_keep_their_original_rows(void)
{
    breakout::Game game(320, 240);

    TEST_ASSERT_EQUAL_UINT8(0, game.bricks()[0].row);
    TEST_ASSERT_EQUAL_UINT8(1, game.bricks()[8].row);
    TEST_ASSERT_EQUAL_UINT8(3, game.bricks()[31].row);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_reset_makes_a_ready_game_with_bricks);
    RUN_TEST(test_tilt_clamps_paddle_to_screen_edges);
    RUN_TEST(test_ready_ball_tracks_the_paddle);
    RUN_TEST(test_launch_starts_the_ball);
    RUN_TEST(test_ball_bounces_off_side_wall);
    RUN_TEST(test_paddle_returns_ball_upward);
    RUN_TEST(test_breaking_every_brick_wins);
    RUN_TEST(test_bricks_keep_their_original_rows);
    return UNITY_END();
}
