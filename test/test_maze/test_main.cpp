#include <unity.h>

#include "maze.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_reset_builds_a_ready_game_with_walls(void)
{
    maze::Game game(320, 240);

    TEST_ASSERT_EQUAL(maze::State::Ready, game.state());
    TEST_ASSERT_TRUE(game.walls().size() >= 10);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 23.0f, game.ball().x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, game.ball().y);
}

void test_ready_game_does_not_move(void)
{
    maze::Game game(320, 240);
    game.setTilt(1.0f, 1.0f);
    game.update(1.0f);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 23.0f, game.ball().x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, game.ball().y);
}

void test_tilt_accelerates_ball_after_start(void)
{
    maze::Game game(320, 240);
    game.start();
    game.setTilt(1.0f, 0.0f);
    game.update(0.1f);

    TEST_ASSERT_TRUE(game.ball().vx > 0.0f);
    TEST_ASSERT_TRUE(game.ball().x > 23.0f);
}

void test_outer_wall_keeps_ball_inside(void)
{
    maze::Game game(320, 240);
    game.start();
    game.setTilt(-1.0f, -1.0f);
    for (int i = 0; i < 1500; ++i) game.update(1.0f / 120.0f);

    TEST_ASSERT_TRUE(game.ball().x >= game.ball().radius + 5.0f - 0.1f);
    TEST_ASSERT_TRUE(game.ball().y >= game.ball().radius + 5.0f - 0.1f);
}

void test_reset_stops_and_restores_start(void)
{
    maze::Game game(320, 240);
    game.start();
    game.setTilt(1.0f, 0.0f);
    game.update(0.5f);
    game.reset();

    TEST_ASSERT_EQUAL(maze::State::Ready, game.state());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 23.0f, game.ball().x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, game.ball().y);
}

void test_maze_has_traps(void)
{
    maze::Game game(320, 240);

    TEST_ASSERT_EQUAL_size_t(4, game.traps().size());
    TEST_ASSERT_EQUAL_UINT32(0, game.trapHits());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_reset_builds_a_ready_game_with_walls);
    RUN_TEST(test_ready_game_does_not_move);
    RUN_TEST(test_tilt_accelerates_ball_after_start);
    RUN_TEST(test_outer_wall_keeps_ball_inside);
    RUN_TEST(test_reset_stops_and_restores_start);
    RUN_TEST(test_maze_has_traps);
    return UNITY_END();
}
