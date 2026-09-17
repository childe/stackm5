#include <unity.h>

#include "vocab.h"

void setUp(void)
{
}

void tearDown(void)
{
}

// 最基本的一条：word | definition | example 三段
void test_parses_three_fields(void)
{
    vocab::WordList l = vocab::parse("abandon | to leave behind | He abandoned the car.");

    TEST_ASSERT_TRUE(l.error.ok);
    TEST_ASSERT_EQUAL_size_t(1, l.words.size());
    TEST_ASSERT_EQUAL_STRING("abandon", l.words[0].word.c_str());
    TEST_ASSERT_EQUAL_STRING("to leave behind", l.words[0].definition.c_str());
    TEST_ASSERT_EQUAL_STRING("He abandoned the car.", l.words[0].example.c_str());
}

// 例句可以省略：只有两段时 example 为空字符串
void test_example_is_optional(void)
{
    vocab::WordList l = vocab::parse("brief | lasting only a short time");

    TEST_ASSERT_TRUE(l.error.ok);
    TEST_ASSERT_EQUAL_size_t(1, l.words.size());
    TEST_ASSERT_EQUAL_STRING("brief", l.words[0].word.c_str());
    TEST_ASSERT_EQUAL_STRING("lasting only a short time", l.words[0].definition.c_str());
    TEST_ASSERT_EQUAL_STRING("", l.words[0].example.c_str());
}

// 空行和 # 注释要忽略，不能变成垃圾词条
void test_skips_blank_lines_and_comments(void)
{
    vocab::WordList l = vocab::parse(
        "\n"
        "# word | definition | example\n"
        "\n"
        "abandon | to leave behind | He left.\n"
        "   \n"
        "  # 缩进的注释也算注释\n"
        "brief | short\n");

    TEST_ASSERT_TRUE(l.error.ok);
    TEST_ASSERT_EQUAL_size_t(2, l.words.size());
    TEST_ASSERT_EQUAL_STRING("abandon", l.words[0].word.c_str());
    TEST_ASSERT_EQUAL_STRING("brief", l.words[1].word.c_str());
}

// 没有 | 就是缺释义，要报错并指出行号（1-based）
void test_missing_definition_is_an_error(void)
{
    vocab::WordList l = vocab::parse(
        "abandon | to leave behind\n"
        "oops\n");

    TEST_ASSERT_FALSE(l.error.ok);
    TEST_ASSERT_EQUAL_size_t(2, l.error.line);
}

// 有 | 但释义是空的，同样是错误
void test_empty_definition_is_an_error(void)
{
    vocab::WordList l = vocab::parse("abandon |  | He left.\n");

    TEST_ASSERT_FALSE(l.error.ok);
    TEST_ASSERT_EQUAL_size_t(1, l.error.line);
}

// 单词是空的也是错误（比如手滑打成 "| definition"）
void test_empty_word_is_an_error(void)
{
    vocab::WordList l = vocab::parse("  | to leave behind\n");

    TEST_ASSERT_FALSE(l.error.ok);
    TEST_ASSERT_EQUAL_size_t(1, l.error.line);
}

// 行号要跳过注释和空行，指向真正出错的那一行
void test_error_line_number_counts_all_lines(void)
{
    vocab::WordList l = vocab::parse(
        "# comment\n"   // 第 1 行
        "\n"            // 第 2 行
        "ok | fine\n"   // 第 3 行
        "\n"            // 第 4 行
        "broken\n");    // 第 5 行 ← 错在这

    TEST_ASSERT_FALSE(l.error.ok);
    TEST_ASSERT_EQUAL_size_t(5, l.error.line);
}

// 只取前两个 | 作分隔，所以例句本身可以含 |
void test_example_may_contain_pipes(void)
{
    vocab::WordList l = vocab::parse("a | b | c | d | e");

    TEST_ASSERT_TRUE(l.error.ok);
    TEST_ASSERT_EQUAL_size_t(1, l.words.size());
    TEST_ASSERT_EQUAL_STRING("a", l.words[0].word.c_str());
    TEST_ASSERT_EQUAL_STRING("b", l.words[0].definition.c_str());
    TEST_ASSERT_EQUAL_STRING("c | d | e", l.words[0].example.c_str());
}

// 最后一行没有换行符时也要能解析（原始字符串字面量末尾常见）
void test_last_line_without_newline(void)
{
    vocab::WordList l = vocab::parse("a | b\nc | d");

    TEST_ASSERT_TRUE(l.error.ok);
    TEST_ASSERT_EQUAL_size_t(2, l.words.size());
    TEST_ASSERT_EQUAL_STRING("c", l.words[1].word.c_str());
}

// 空输入不算错，只是没有词
void test_empty_input_is_not_an_error(void)
{
    vocab::WordList l = vocab::parse("");

    TEST_ASSERT_TRUE(l.error.ok);
    TEST_ASSERT_EQUAL_size_t(0, l.words.size());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_example_may_contain_pipes);
    RUN_TEST(test_last_line_without_newline);
    RUN_TEST(test_empty_input_is_not_an_error);
    RUN_TEST(test_missing_definition_is_an_error);
    RUN_TEST(test_empty_definition_is_an_error);
    RUN_TEST(test_empty_word_is_an_error);
    RUN_TEST(test_error_line_number_counts_all_lines);
    RUN_TEST(test_skips_blank_lines_and_comments);
    RUN_TEST(test_parses_three_fields);
    RUN_TEST(test_example_is_optional);
    return UNITY_END();
}
