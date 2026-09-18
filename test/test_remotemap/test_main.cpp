#include <unity.h>

#include <set>
#include <string>

#include "remotemap.h"

void setUp(void)
{
}

void tearDown(void)
{
}

using remotemap::Action;

// 方向键沿用 Cardputer 社区惯例 ; . , /
void test_dpad_keys(void)
{
    TEST_ASSERT_TRUE(Action::Up == remotemap::fromChar(';'));
    TEST_ASSERT_TRUE(Action::Down == remotemap::fromChar('.'));
    TEST_ASSERT_TRUE(Action::Left == remotemap::fromChar(','));
    TEST_ASSERT_TRUE(Action::Right == remotemap::fromChar('/'));
}

void test_volume_keys(void)
{
    TEST_ASSERT_TRUE(Action::VolUp == remotemap::fromChar('='));
    TEST_ASSERT_TRUE(Action::VolDown == remotemap::fromChar('-'));
    TEST_ASSERT_TRUE(Action::Mute == remotemap::fromChar('m'));
}

void test_navigation_keys(void)
{
    TEST_ASSERT_TRUE(Action::Back == remotemap::fromChar('b'));
    TEST_ASSERT_TRUE(Action::Home == remotemap::fromChar('h'));
    TEST_ASSERT_TRUE(Action::Power == remotemap::fromChar('p'));
    TEST_ASSERT_TRUE(Action::ExitApp == remotemap::fromChar('`'));
}

// ⏎ 不在 word 里，走单独入口
void test_enter_is_ok(void)
{
    TEST_ASSERT_TRUE(Action::Ok == remotemap::fromEnter());
}

void test_unmapped_keys_are_none(void)
{
    TEST_ASSERT_TRUE(Action::None == remotemap::fromChar('x'));
    TEST_ASSERT_TRUE(Action::None == remotemap::fromChar('1'));
    TEST_ASSERT_TRUE(Action::None == remotemap::fromChar(' '));
    TEST_ASSERT_TRUE(Action::None == remotemap::fromChar('\0'));
}

// 核心：没有两个键映射到同一个动作。手改映射表时最容易犯的错。
void test_no_two_keys_share_an_action(void)
{
    std::set<int> seen;
    for (size_t i = 0; i < remotemap::kBindingCount; ++i) {
        const int a = static_cast<int>(remotemap::kBindings[i].action);
        TEST_ASSERT_TRUE_MESSAGE(seen.insert(a).second, "两个键映射到了同一个动作");
    }
}

// 反向：没有两个条目用同一个键（后一个会被前一个遮住，静默失效）
void test_no_duplicate_keys(void)
{
    std::set<char> seen;
    for (size_t i = 0; i < remotemap::kBindingCount; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(seen.insert(remotemap::kBindings[i].key).second,
                                 "同一个键出现了两次");
    }
}

// 映射表和 fromChar 必须一致 —— 防止改了表忘了改查找函数
void test_table_matches_lookup(void)
{
    for (size_t i = 0; i < remotemap::kBindingCount; ++i) {
        const remotemap::Binding &b = remotemap::kBindings[i];
        TEST_ASSERT_TRUE(b.action == remotemap::fromChar(b.key));
    }
}

// 除了 None 和 Ok（Ok 绑在 ⏎ 上，不是可打印键），每个动作都要有键
void test_every_action_has_a_key(void)
{
    std::set<int> mapped;
    for (size_t i = 0; i < remotemap::kBindingCount; ++i) {
        mapped.insert(static_cast<int>(remotemap::kBindings[i].action));
    }

    const Action needsKey[] = {Action::Up,   Action::Down,  Action::Left,    Action::Right,
                               Action::Back, Action::Home,  Action::Mute,    Action::VolUp,
                               Action::VolDown, Action::Power, Action::ExitApp};
    for (const Action a : needsKey) {
        TEST_ASSERT_TRUE_MESSAGE(mapped.count(static_cast<int>(a)) == 1,
                                 remotemap::label(a));
    }
}

// 每个动作都要有非空的显示名，屏幕上才有东西可显示
void test_labels_are_present(void)
{
    const Action all[] = {Action::Up,      Action::Down,  Action::Left,  Action::Right,
                          Action::Ok,      Action::Back,  Action::Home,  Action::Mute,
                          Action::VolUp,   Action::VolDown, Action::Power, Action::ExitApp};
    for (const Action a : all) {
        const char *l = remotemap::label(a);
        TEST_ASSERT_NOT_NULL(l);
        TEST_ASSERT_TRUE(std::string(l).size() > 0);
    }
    TEST_ASSERT_EQUAL_STRING("-", remotemap::label(Action::None));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_dpad_keys);
    RUN_TEST(test_volume_keys);
    RUN_TEST(test_navigation_keys);
    RUN_TEST(test_enter_is_ok);
    RUN_TEST(test_unmapped_keys_are_none);
    RUN_TEST(test_no_two_keys_share_an_action);
    RUN_TEST(test_no_duplicate_keys);
    RUN_TEST(test_table_matches_lookup);
    RUN_TEST(test_every_action_has_a_key);
    RUN_TEST(test_labels_are_present);
    return UNITY_END();
}
