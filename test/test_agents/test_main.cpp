#include <unity.h>

#include <string>
#include <vector>

#include "agents.h"

using agents::Model;
using agents::State;

void setUp(void)
{
}

void tearDown(void)
{
}

// 依次喂 B、各行、E
static void feedList(Model &m, const std::vector<std::string> &lines)
{
    m.feedLine("B");
    for (const auto &l : lines) m.feedLine(l);
    m.feedLine("E");
}

// 只给 id 和状态，其它字段用固定值
static std::string S(const std::string &id, char state)
{
    return "S\t" + id + "\tc\t" + std::string(1, state) + "\tT\t~/p";
}

void test_parses_full_list(void)
{
    Model m;
    feedList(m, {"S\tid1\tc\tr\t标题一\t~/p", "S\tid2\tx\tw\tT2\t~/q"});
    TEST_ASSERT_EQUAL_size_t(2, m.sessions().size());
    TEST_ASSERT_EQUAL_CHAR('c', m.sessions()[0].agent);
    TEST_ASSERT_TRUE(m.sessions()[0].state == State::Running);
    TEST_ASSERT_TRUE(m.sessions()[1].state == State::Waiting);
    TEST_ASSERT_EQUAL_STRING("标题一", m.sessions()[0].title.c_str());
    TEST_ASSERT_EQUAL_STRING("~/q", m.sessions()[1].dir.c_str());
}

void test_list_replaced_only_on_E(void)
{
    Model m;
    feedList(m, {S("id1", 'r')});
    m.feedLine("B");
    m.feedLine(S("id2", 'r'));
    TEST_ASSERT_EQUAL_size_t(1, m.sessions().size());
    TEST_ASSERT_EQUAL_STRING("id1", m.sessions()[0].id.c_str());
    TEST_ASSERT_TRUE(m.feedLine("E"));
    TEST_ASSERT_EQUAL_STRING("id2", m.sessions()[0].id.c_str());
}

void test_drops_S_before_B_and_short_lines(void)
{
    Model m;
    m.feedLine(S("id0", 'r'));
    m.feedLine("B");
    m.feedLine("S\tid1\tc\tr");
    m.feedLine("E");
    TEST_ASSERT_EQUAL_size_t(0, m.sessions().size());
}

void test_first_list_is_baseline(void)
{
    Model m;
    feedList(m, {S("id1", 'w')});
    TEST_ASSERT_EQUAL_size_t(0, m.lastAlerts().size());
    TEST_ASSERT_FALSE(m.marked("id1"));
}

void test_running_to_waiting_alerts_and_moves_cursor(void)
{
    Model m;
    feedList(m, {S("id1", 'w'), S("id2", 'r'), S("id3", 'r')});
    feedList(m, {S("id1", 'w'), S("id2", 'w'), S("id3", 'w')});
    TEST_ASSERT_EQUAL_size_t(2, m.lastAlerts().size());
    TEST_ASSERT_EQUAL_STRING("id2", m.lastAlerts()[0].c_str());
    TEST_ASSERT_EQUAL_STRING("id3", m.lastAlerts()[1].c_str());
    TEST_ASSERT_TRUE(m.marked("id2"));
    TEST_ASSERT_TRUE(m.marked("id3"));
    TEST_ASSERT_EQUAL_STRING("id2", m.selected()->id.c_str());
}

void test_new_session_does_not_alert(void)
{
    Model m;
    feedList(m, {S("id1", 'r')});
    feedList(m, {S("id1", 'r'), S("id9", 'w')});
    TEST_ASSERT_EQUAL_size_t(0, m.lastAlerts().size());
}

void test_alerts_cleared_on_next_commit(void)
{
    Model m;
    feedList(m, {S("id1", 'r')});
    feedList(m, {S("id1", 'w')});
    feedList(m, {S("id1", 'w')});
    TEST_ASSERT_EQUAL_size_t(0, m.lastAlerts().size());
    TEST_ASSERT_TRUE(m.marked("id1"));
}

void test_cursor_follows_id_across_reorder(void)
{
    Model m;
    feedList(m, {S("id1", 'r'), S("id2", 'r'), S("id3", 'r')});
    m.moveCursor(+1);
    feedList(m, {S("id3", 'r'), S("id2", 'r'), S("id1", 'r')});
    TEST_ASSERT_EQUAL_STRING("id2", m.selected()->id.c_str());
}

void test_cursor_stays_on_row_when_session_vanishes(void)
{
    Model m;
    feedList(m, {S("id1", 'r'), S("id2", 'r'), S("id3", 'r')});
    m.moveCursor(+2);
    feedList(m, {S("id1", 'r'), S("id2", 'r')});
    TEST_ASSERT_EQUAL_INT(1, m.cursor());
}

void test_move_clears_mark_and_clamps(void)
{
    Model m;
    feedList(m, {S("id1", 'w'), S("id2", 'r'), S("id3", 'r')});
    feedList(m, {S("id1", 'w'), S("id2", 'w'), S("id3", 'r')});
    m.moveCursor(-1);  // 提醒把光标放到 id2，先移回 id1
    TEST_ASSERT_EQUAL_INT(0, m.cursor());
    TEST_ASSERT_TRUE(m.marked("id2"));
    m.moveCursor(+1);
    TEST_ASSERT_FALSE(m.marked("id2"));
    m.moveCursor(+5);
    TEST_ASSERT_EQUAL_INT(2, m.cursor());
    m.moveCursor(-9);
    TEST_ASSERT_EQUAL_INT(0, m.cursor());
}

void test_focus_selected(void)
{
    Model m;
    feedList(m, {S("id1", 'w'), S("id2", 'r')});
    feedList(m, {S("id1", 'w'), S("id2", 'w')});
    TEST_ASSERT_TRUE(m.marked("id2"));
    TEST_ASSERT_EQUAL_STRING("F\tid2", m.focusSelected().c_str());
    TEST_ASSERT_FALSE(m.marked("id2"));
}

void test_empty_list_has_no_selection(void)
{
    Model m;
    feedList(m, {});
    TEST_ASSERT_EQUAL_INT(-1, m.cursor());
    TEST_ASSERT_NULL(m.selected());
    TEST_ASSERT_EQUAL_STRING("", m.focusSelected().c_str());
    m.moveCursor(1);
    TEST_ASSERT_EQUAL_INT(-1, m.cursor());
}

void test_empty_to_nonempty_selects_first(void)
{
    Model m;
    feedList(m, {});
    feedList(m, {S("id1", 'r'), S("id2", 'r')});
    TEST_ASSERT_EQUAL_INT(0, m.cursor());
}

void test_vanished_session_loses_mark(void)
{
    Model m;
    feedList(m, {S("id1", 'r')});
    feedList(m, {S("id1", 'w')});
    feedList(m, {});
    feedList(m, {S("id1", 'w')});
    TEST_ASSERT_FALSE(m.marked("id1"));
}

void test_reset_drops_partial_list(void)
{
    Model m;
    feedList(m, {S("id1", 'r')});
    m.feedLine("B");
    m.feedLine(S("id7", 'r'));
    m.reset();
    TEST_ASSERT_EQUAL_size_t(0, m.sessions().size());
    feedList(m, {S("id2", 'w')});
    TEST_ASSERT_EQUAL_size_t(1, m.sessions().size());
    TEST_ASSERT_EQUAL_STRING("id2", m.sessions()[0].id.c_str());
    TEST_ASSERT_EQUAL_size_t(0, m.lastAlerts().size());
}

void test_strips_trailing_newline(void)
{
    Model m;
    m.feedLine("B\n");
    m.feedLine(S("id1", 'r') + "\n");
    TEST_ASSERT_TRUE(m.feedLine("E\n"));
    TEST_ASSERT_EQUAL_size_t(1, m.sessions().size());
    TEST_ASSERT_EQUAL_STRING("~/p", m.sessions()[0].dir.c_str());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_parses_full_list);
    RUN_TEST(test_list_replaced_only_on_E);
    RUN_TEST(test_drops_S_before_B_and_short_lines);
    RUN_TEST(test_first_list_is_baseline);
    RUN_TEST(test_running_to_waiting_alerts_and_moves_cursor);
    RUN_TEST(test_new_session_does_not_alert);
    RUN_TEST(test_alerts_cleared_on_next_commit);
    RUN_TEST(test_cursor_follows_id_across_reorder);
    RUN_TEST(test_cursor_stays_on_row_when_session_vanishes);
    RUN_TEST(test_move_clears_mark_and_clamps);
    RUN_TEST(test_focus_selected);
    RUN_TEST(test_empty_list_has_no_selection);
    RUN_TEST(test_empty_to_nonempty_selects_first);
    RUN_TEST(test_vanished_session_loses_mark);
    RUN_TEST(test_reset_drops_partial_list);
    RUN_TEST(test_strips_trailing_newline);
    return UNITY_END();
}
