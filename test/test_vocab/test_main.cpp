#include <unity.h>

#include <cstdarg>
#include <cstdio>
#include <set>
#include <string>

#include "texted.h"
#include "vocab.h"
#include "wordlist.h"

void setUp(void)
{
}

void tearDown(void)
{
}

// 完整四段：word | phonetic | definition | example
void test_parses_four_fields(void)
{
    vocab::WordList l =
        vocab::parse("abandon | /ə'bandən/ | to leave behind | He abandoned the car.");

    TEST_ASSERT_TRUE(l.error.ok);
    TEST_ASSERT_EQUAL_size_t(1, l.words.size());
    TEST_ASSERT_EQUAL_STRING("abandon", l.words[0].word.c_str());
    TEST_ASSERT_EQUAL_STRING("/ə'bandən/", l.words[0].phonetic.c_str());
    TEST_ASSERT_EQUAL_STRING("to leave behind", l.words[0].definition.c_str());
    TEST_ASSERT_EQUAL_STRING("He abandoned the car.", l.words[0].example.c_str());
}

// 音标可以留空（两个 | 之间什么都没有）
void test_phonetic_may_be_empty(void)
{
    vocab::WordList l = vocab::parse("brief |  | lasting only a short time | It was brief.");

    TEST_ASSERT_TRUE(l.error.ok);
    TEST_ASSERT_EQUAL_STRING("brief", l.words[0].word.c_str());
    TEST_ASSERT_EQUAL_STRING("", l.words[0].phonetic.c_str());
    TEST_ASSERT_EQUAL_STRING("lasting only a short time", l.words[0].definition.c_str());
}

// 例句可以省略：只有三段时 example 为空字符串
void test_example_is_optional(void)
{
    vocab::WordList l = vocab::parse("brief | /bri:f/ | lasting only a short time");

    TEST_ASSERT_TRUE(l.error.ok);
    TEST_ASSERT_EQUAL_size_t(1, l.words.size());
    TEST_ASSERT_EQUAL_STRING("brief", l.words[0].word.c_str());
    TEST_ASSERT_EQUAL_STRING("/bri:f/", l.words[0].phonetic.c_str());
    TEST_ASSERT_EQUAL_STRING("lasting only a short time", l.words[0].definition.c_str());
    TEST_ASSERT_EQUAL_STRING("", l.words[0].example.c_str());
}

// 只有两段（连释义都没有）是错误
void test_two_fields_is_an_error(void)
{
    vocab::WordList l = vocab::parse("brief | /bri:f/\n");

    TEST_ASSERT_FALSE(l.error.ok);
    TEST_ASSERT_EQUAL_size_t(1, l.error.line);
}

// 空行和 # 注释要忽略，不能变成垃圾词条
void test_skips_blank_lines_and_comments(void)
{
    vocab::WordList l = vocab::parse(
        "\n"
        "# word | definition | example\n"
        "\n"
        "abandon | /x/ | to leave behind | He left.\n"
        "   \n"
        "  # 缩进的注释也算注释\n"
        "brief | /y/ | short\n");

    TEST_ASSERT_TRUE(l.error.ok);
    TEST_ASSERT_EQUAL_size_t(2, l.words.size());
    TEST_ASSERT_EQUAL_STRING("abandon", l.words[0].word.c_str());
    TEST_ASSERT_EQUAL_STRING("brief", l.words[1].word.c_str());
}

// 没有 | 就是缺释义，要报错并指出行号（1-based）
void test_missing_definition_is_an_error(void)
{
    vocab::WordList l = vocab::parse(
        "abandon | /x/ | to leave behind\n"
        "oops\n");

    TEST_ASSERT_FALSE(l.error.ok);
    TEST_ASSERT_EQUAL_size_t(2, l.error.line);
}

// 有 | 但释义是空的，同样是错误
void test_empty_definition_is_an_error(void)
{
    vocab::WordList l = vocab::parse("abandon | /x/ |  | He left.\n");

    TEST_ASSERT_FALSE(l.error.ok);
    TEST_ASSERT_EQUAL_size_t(1, l.error.line);
}

// 单词是空的也是错误（比如手滑打成 "| definition"）
void test_empty_word_is_an_error(void)
{
    vocab::WordList l = vocab::parse("  | /x/ | to leave behind\n");

    TEST_ASSERT_FALSE(l.error.ok);
    TEST_ASSERT_EQUAL_size_t(1, l.error.line);
}

// 行号要跳过注释和空行，指向真正出错的那一行
void test_error_line_number_counts_all_lines(void)
{
    vocab::WordList l = vocab::parse(
        "# comment\n"   // 第 1 行
        "\n"            // 第 2 行
        "ok | /x/ | fine\n"   // 第 3 行
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
    TEST_ASSERT_EQUAL_STRING("b", l.words[0].phonetic.c_str());
    TEST_ASSERT_EQUAL_STRING("c", l.words[0].definition.c_str());
    TEST_ASSERT_EQUAL_STRING("d | e", l.words[0].example.c_str());
}

// 最后一行没有换行符时也要能解析（原始字符串字面量末尾常见）
void test_last_line_without_newline(void)
{
    vocab::WordList l = vocab::parse("a | b | c\nd | e | f");

    TEST_ASSERT_TRUE(l.error.ok);
    TEST_ASSERT_EQUAL_size_t(2, l.words.size());
    TEST_ASSERT_EQUAL_STRING("d", l.words[1].word.c_str());
}

// 空输入不算错，只是没有词
void test_empty_input_is_not_an_error(void)
{
    vocab::WordList l = vocab::parse("");

    TEST_ASSERT_TRUE(l.error.ok);
    TEST_ASSERT_EQUAL_size_t(0, l.words.size());
}

// ── 内置词表对账 ────────────────────────────────────────────
//
// 词表是手写的 100 条，最容易出的错：释义/例句超出屏幕、单词写重复、
// 手滑打出中文引号或长破折号（AsciiFont8x16 显示不出来）。
//
// 实现上两个坑（都是实测的）：Unity 的断言在第一个失败处就中止整个测试
// 函数，所以不能在循环里直接断言；PlatformIO 的测试运行器会过滤掉
// TEST_MESSAGE 的输出，所以明细必须拼进断言自己的 message 里。

namespace {

// 释义和例句用 FreeMono12pt（14x24 等宽）→ 240/14 = 17 列。
// 两步布局下各占一整块，所以能放 3 行（3x24 = 72px）。
constexpr size_t kBodyCols = 17;
constexpr size_t kBodyLines = 3;

char gReport[600];
size_t gUsed = 0;

void reportReset()
{
    gReport[0] = '\0';
    gUsed = 0;
}

void reportAdd(const char *fmt, ...)
{
    if (gUsed + 1 >= sizeof(gReport)) return;

    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(gReport + gUsed, sizeof(gReport) - gUsed, fmt, ap);
    va_end(ap);

    if (n > 0) gUsed += static_cast<size_t>(n);
    if (gUsed >= sizeof(gReport)) gUsed = sizeof(gReport) - 1;
}

}  // namespace

void test_builtin_wordlist_parses(void)
{
    vocab::WordList l = vocab::parse(vocab::kRawWords);

    reportReset();
    if (!l.error.ok) {
        reportAdd("line %u: %s", static_cast<unsigned>(l.error.line), l.error.reason);
    }
    TEST_ASSERT_TRUE_MESSAGE(l.error.ok, gReport);
}

void test_builtin_wordlist_has_expected_count(void)
{
    vocab::WordList l = vocab::parse(vocab::kRawWords);

    TEST_ASSERT_TRUE(l.error.ok);
    TEST_ASSERT_EQUAL_size_t(vocab::kExpectedWordCount, l.words.size());
}

// 释义和例句必须放得下屏幕：FreeMono12pt 是 17 列，各画 3 行。
//
// 断言的是「折行之后占几行」而不是字符数 —— 这两者不等价：按单词贪心
// 折行时字符数够但行数超的情况是存在的，而多出来的行会被静默丢掉。
// 只查字符数的话这种词条能通过测试，却在设备上显示不全。
void test_builtin_wordlist_fits_on_screen(void)
{
    vocab::WordList l = vocab::parse(vocab::kRawWords);
    TEST_ASSERT_TRUE(l.error.ok);

    reportReset();
    size_t bad = 0;
    for (const vocab::Word &w : l.words) {
        const size_t defLines = texted::wrapLines(w.definition, kBodyCols).size();
        const size_t exLines = texted::wrapLines(w.example, kBodyCols).size();

        if (defLines > kBodyLines) {
            reportAdd("[%s def %u lines] ", w.word.c_str(), static_cast<unsigned>(defLines));
            ++bad;
        }
        if (exLines > kBodyLines) {
            reportAdd("[%s ex %u lines] ", w.word.c_str(), static_cast<unsigned>(exLines));
            ++bad;
        }
    }

    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, bad, gReport);
}

// 字符数上限仍然查一遍：它是写词表时更直观的指标，而且比折行更严
void test_builtin_wordlist_respects_char_limits(void)
{
    vocab::WordList l = vocab::parse(vocab::kRawWords);
    TEST_ASSERT_TRUE(l.error.ok);

    reportReset();
    size_t bad = 0;
    for (const vocab::Word &w : l.words) {
        if (w.definition.size() > vocab::kMaxDefinitionChars) {
            reportAdd("[%s def %u] ", w.word.c_str(), static_cast<unsigned>(w.definition.size()));
            ++bad;
        }
        if (w.example.size() > vocab::kMaxExampleChars) {
            reportAdd("[%s ex %u] ", w.word.c_str(), static_cast<unsigned>(w.example.size()));
            ++bad;
        }
    }

    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, bad, gReport);
}

// AsciiFont8x16 只有 ASCII 可打印字符。中文引号、长破折号这类字符
// 在设备上显示不出来，必须在这里拦住。
void test_builtin_wordlist_is_pure_ascii(void)
{
    vocab::WordList l = vocab::parse(vocab::kRawWords);
    TEST_ASSERT_TRUE(l.error.ok);

    reportReset();
    size_t bad = 0;
    for (const vocab::Word &w : l.words) {
        const std::string all = w.word + w.definition + w.example;
        for (const char c : all) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u < 0x20 || u > 0x7E) {
                reportAdd("[%s byte 0x%02X] ", w.word.c_str(), u);
                ++bad;
                break;
            }
        }
    }

    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, bad, gReport);
}

// 音标只能用白名单里的符号。
//
// 这条是唯一能挡住「设备上显示成方块」的防线：efontJA_16 缺 ɪ ɛ ɝ，
// 前两个用手写字形补了、ɝ 要改写成 ɜr。用错符号在 Mac 上完全看不出来。
void test_builtin_phonetics_use_whitelisted_symbols_only(void)
{
    vocab::WordList l = vocab::parse(vocab::kRawWords);
    TEST_ASSERT_TRUE(l.error.ok);

    const std::vector<uint32_t> allowedVec = vocab::toCodepoints(vocab::kPhoneticWhitelist);
    const std::set<uint32_t> allowed(allowedVec.begin(), allowedVec.end());
    TEST_ASSERT_TRUE(allowed.size() > 30);  // 白名单本身别写坏了

    reportReset();
    size_t bad = 0;
    for (const vocab::Word &w : l.words) {
        if (w.phonetic.empty()) continue;
        for (const uint32_t cp : vocab::toCodepoints(w.phonetic.c_str())) {
            if (allowed.count(cp) == 0) {
                reportAdd("[%s U+%04X] ", w.word.c_str(), static_cast<unsigned>(cp));
                ++bad;
                break;
            }
        }
    }

    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, bad, gReport);
}

// 音标要放得下一行：8px 等宽，屏幕 30 个字形
void test_builtin_phonetics_fit_on_one_line(void)
{
    vocab::WordList l = vocab::parse(vocab::kRawWords);
    TEST_ASSERT_TRUE(l.error.ok);

    reportReset();
    size_t bad = 0;
    for (const vocab::Word &w : l.words) {
        const size_t glyphs = vocab::toCodepoints(w.phonetic.c_str()).size();
        if (glyphs > vocab::kMaxPhoneticGlyphs) {
            reportAdd("[%s %u glyphs] ", w.word.c_str(), static_cast<unsigned>(glyphs));
            ++bad;
        }
    }

    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, bad, gReport);
}

// 每条都要有音标 —— 格式允许留空，但内置词表不该偷懒
void test_builtin_wordlist_every_word_has_a_phonetic(void)
{
    vocab::WordList l = vocab::parse(vocab::kRawWords);
    TEST_ASSERT_TRUE(l.error.ok);

    reportReset();
    size_t bad = 0;
    for (const vocab::Word &w : l.words) {
        if (w.phonetic.empty()) {
            reportAdd("[%s] ", w.word.c_str());
            ++bad;
        }
    }

    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, bad, gReport);
}

void test_builtin_wordlist_has_no_duplicates(void)
{
    vocab::WordList l = vocab::parse(vocab::kRawWords);
    TEST_ASSERT_TRUE(l.error.ok);

    reportReset();
    std::set<std::string> seen;
    size_t bad = 0;
    for (const vocab::Word &w : l.words) {
        if (!seen.insert(w.word).second) {
            reportAdd("[dup %s] ", w.word.c_str());
            ++bad;
        }
    }

    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, bad, gReport);
}

// 每条都要有例句 —— 格式允许省略，但内置词表不该偷懒
void test_builtin_wordlist_every_word_has_an_example(void)
{
    vocab::WordList l = vocab::parse(vocab::kRawWords);
    TEST_ASSERT_TRUE(l.error.ok);

    reportReset();
    size_t bad = 0;
    for (const vocab::Word &w : l.words) {
        if (w.example.empty()) {
            reportAdd("[%s no example] ", w.word.c_str());
            ++bad;
        }
    }

    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, bad, gReport);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_builtin_wordlist_parses);
    RUN_TEST(test_builtin_wordlist_has_expected_count);
    RUN_TEST(test_builtin_wordlist_fits_on_screen);
    RUN_TEST(test_builtin_wordlist_respects_char_limits);
    RUN_TEST(test_builtin_wordlist_is_pure_ascii);
    RUN_TEST(test_builtin_phonetics_use_whitelisted_symbols_only);
    RUN_TEST(test_builtin_phonetics_fit_on_one_line);
    RUN_TEST(test_builtin_wordlist_every_word_has_a_phonetic);
    RUN_TEST(test_builtin_wordlist_has_no_duplicates);
    RUN_TEST(test_builtin_wordlist_every_word_has_an_example);
    RUN_TEST(test_example_may_contain_pipes);
    RUN_TEST(test_last_line_without_newline);
    RUN_TEST(test_empty_input_is_not_an_error);
    RUN_TEST(test_missing_definition_is_an_error);
    RUN_TEST(test_empty_definition_is_an_error);
    RUN_TEST(test_empty_word_is_an_error);
    RUN_TEST(test_error_line_number_counts_all_lines);
    RUN_TEST(test_skips_blank_lines_and_comments);
    RUN_TEST(test_parses_four_fields);
    RUN_TEST(test_phonetic_may_be_empty);
    RUN_TEST(test_two_fields_is_an_error);
    RUN_TEST(test_example_is_optional);
    return UNITY_END();
}
