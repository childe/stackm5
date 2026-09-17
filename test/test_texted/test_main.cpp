#include <unity.h>

#include "texted.h"

void setUp(void)
{
}

void tearDown(void)
{
}

// 打字：插入在光标处，光标跟着右移
void test_insert_appends_and_advances_cursor(void)
{
    texted::Buffer b;
    b.insert('3');
    b.insert(' ');
    b.insert('5');

    TEST_ASSERT_EQUAL_STRING("3 5", b.text().c_str());
    TEST_ASSERT_EQUAL_size_t(3, b.cursor());
}

// 在中间插入：光标左边的内容不动，右边的往后让
void test_insert_in_the_middle(void)
{
    texted::Buffer b;
    b.setText("35");
    b.moveHome();
    b.moveRight();
    b.insert(' ');

    TEST_ASSERT_EQUAL_STRING("3 5", b.text().c_str());
    TEST_ASSERT_EQUAL_size_t(2, b.cursor());
}

// 退格删光标「前面」那个字符；光标在最左边时退格什么也不做
void test_backspace_deletes_char_before_cursor(void)
{
    texted::Buffer b;
    b.setText("3 5");
    b.moveEnd();
    b.backspace();

    TEST_ASSERT_EQUAL_STRING("3 ", b.text().c_str());
    TEST_ASSERT_EQUAL_size_t(2, b.cursor());

    b.moveHome();
    b.backspace();
    TEST_ASSERT_EQUAL_STRING("3 ", b.text().c_str());
    TEST_ASSERT_EQUAL_size_t(0, b.cursor());
}

// 光标移动不能越界
void test_cursor_does_not_run_off_either_end(void)
{
    texted::Buffer b;
    b.setText("35");

    b.moveHome();
    b.moveLeft();
    TEST_ASSERT_EQUAL_size_t(0, b.cursor());

    b.moveEnd();
    b.moveRight();
    TEST_ASSERT_EQUAL_size_t(2, b.cursor());
}

// setText 之后光标停在末尾（打开一首已有的谱子时接着往后敲）
void test_set_text_puts_cursor_at_end(void)
{
    texted::Buffer b;
    b.setText("3 3 3-");

    TEST_ASSERT_EQUAL_size_t(6, b.cursor());
}

// 折行：贪心填满一行，只在空格处断开，不把音符拆开。
// 下标: 0123456789...
// 原文: 3 3 3- 5 5 5- 1'
//                 ↑ 下标 10 是空格，断在这里
// cols=10 → "3 3 3- 5 5"（正好 10 个字符）/ "5- 1'"
void test_wrap_breaks_at_spaces_without_splitting_notes(void)
{
    std::vector<texted::Line> lines = texted::wrapLines("3 3 3- 5 5 5- 1'", 10);

    TEST_ASSERT_EQUAL_size_t(2, lines.size());
    TEST_ASSERT_EQUAL_size_t(0, lines[0].start);
    TEST_ASSERT_EQUAL_size_t(10, lines[0].len);  // "3 3 3- 5 5"
    TEST_ASSERT_EQUAL_size_t(11, lines[1].start);
    TEST_ASSERT_EQUAL_size_t(5, lines[1].len);  // "5- 1'"，断点处的空格被吃掉
}

// 一个超长的 token 挤不进一行时只能硬切，不能死循环
void test_wrap_hard_splits_an_oversized_token(void)
{
    std::vector<texted::Line> lines = texted::wrapLines("1---------", 4);

    TEST_ASSERT_EQUAL_size_t(3, lines.size());
    TEST_ASSERT_EQUAL_size_t(4, lines[0].len);
    TEST_ASSERT_EQUAL_size_t(4, lines[1].len);
    TEST_ASSERT_EQUAL_size_t(2, lines[2].len);
}

// 空文本要产出一行空行（不然屏幕上画不出光标）
void test_wrap_of_empty_text_is_one_empty_line(void)
{
    std::vector<texted::Line> lines = texted::wrapLines("", 10);

    TEST_ASSERT_EQUAL_size_t(1, lines.size());
    TEST_ASSERT_EQUAL_size_t(0, lines[0].len);
}

// 光标下标 → 第几行第几列
void test_cursor_row_col(void)
{
    std::vector<texted::Line> lines = texted::wrapLines("3 3 3- 5 5 5- 1'", 10);
    size_t row = 99, col = 99;

    texted::cursorRowCol(lines, 0, row, col);
    TEST_ASSERT_EQUAL_size_t(0, row);
    TEST_ASSERT_EQUAL_size_t(0, col);

    texted::cursorRowCol(lines, 5, row, col);
    TEST_ASSERT_EQUAL_size_t(0, row);
    TEST_ASSERT_EQUAL_size_t(5, col);

    // 第 2 行从下标 11 开始，所以光标 11 在第 2 行第 0 列
    texted::cursorRowCol(lines, 11, row, col);
    TEST_ASSERT_EQUAL_size_t(1, row);
    TEST_ASSERT_EQUAL_size_t(0, col);

    // 光标在文本末尾（下标 16）时落在最后一行的末尾
    texted::cursorRowCol(lines, 16, row, col);
    TEST_ASSERT_EQUAL_size_t(1, row);
    TEST_ASSERT_EQUAL_size_t(5, col);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_insert_appends_and_advances_cursor);
    RUN_TEST(test_insert_in_the_middle);
    RUN_TEST(test_backspace_deletes_char_before_cursor);
    RUN_TEST(test_cursor_does_not_run_off_either_end);
    RUN_TEST(test_set_text_puts_cursor_at_end);
    RUN_TEST(test_wrap_breaks_at_spaces_without_splitting_notes);
    RUN_TEST(test_wrap_hard_splits_an_oversized_token);
    RUN_TEST(test_wrap_of_empty_text_is_one_empty_line);
    RUN_TEST(test_cursor_row_col);
    return UNITY_END();
}
