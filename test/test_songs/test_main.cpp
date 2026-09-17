#include <unity.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "jianpu.h"
#include "songs.h"

void setUp(void)
{
}

void tearDown(void)
{
}

/*
 * 内置谱子是手写的字符串，最容易出的错是漏空格、多符号、拍数算错。
 *
 * 两个实现上的坑，都是实测出来的：
 *   1. Unity 的断言在第一个失败处就中止整个测试函数，所以不能在循环里
 *      直接断言 —— 那样一轮只看得到一首出问题的。
 *   2. PlatformIO 的测试运行器会过滤掉 TEST_MESSAGE 的输出，只保留
 *      「文件:行: 测试名: 结果」格式的行。所以明细必须拼进断言自己的
 *      message 里才看得见。
 *
 * 结论：循环里把所有问题攒成一个字符串，最后断言一次并把它当 message。
 */

namespace {

char gReport[480];
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

void test_every_builtin_parses_without_error(void)
{
    TEST_ASSERT_TRUE(songs::kBuiltinCount > 0);
    reportReset();

    size_t bad = 0;
    for (size_t i = 0; i < songs::kBuiltinCount; ++i) {
        const songs::Builtin &b = songs::kBuiltins[i];
        const std::string text = songs::fullTextOf(b);
        const jianpu::Score s = jianpu::parse(text.c_str(), text.size());

        if (!s.error.ok) {
            reportAdd("[%s at %u: %s] ", b.title, static_cast<unsigned>(s.error.pos),
                      s.error.reason);
            ++bad;
        }
    }

    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, bad, gReport);
}

// 拍数尤其重要：4/4 的 8 小节必须正好 32 拍，差一拍就说明某个时长记号写错了
void test_every_builtin_has_expected_notes_and_beats(void)
{
    reportReset();

    size_t bad = 0;
    for (size_t i = 0; i < songs::kBuiltinCount; ++i) {
        const songs::Builtin &b = songs::kBuiltins[i];
        const std::string text = songs::fullTextOf(b);
        const jianpu::Score s = jianpu::parse(text.c_str(), text.size());

        if (s.notes.size() != b.noteCount) {
            reportAdd("[%s notes %u want %u] ", b.title, static_cast<unsigned>(s.notes.size()),
                      static_cast<unsigned>(b.noteCount));
            ++bad;
        }

        const float beats = s.totalBeats();
        if (beats < b.beats - 0.01f || beats > b.beats + 0.01f) {
            reportAdd("[%s beats %.2f want %.2f] ", b.title, beats, b.beats);
            ++bad;
        }
    }

    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, bad, gReport);
}

void test_builtins_are_well_formed(void)
{
    for (size_t i = 0; i < songs::kBuiltinCount; ++i) {
        const songs::Builtin &b = songs::kBuiltins[i];

        TEST_ASSERT_NOT_NULL(b.title);
        TEST_ASSERT_NOT_NULL(b.header);
        TEST_ASSERT_NOT_NULL(b.notes);
        TEST_ASSERT_TRUE(std::strlen(b.title) > 0);
        TEST_ASSERT_TRUE(std::strlen(b.notes) > 0);
        TEST_ASSERT_TRUE(std::strlen(b.notes) < 512);  // 要塞得进 Flash
    }
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_builtin_parses_without_error);
    RUN_TEST(test_every_builtin_has_expected_notes_and_beats);
    RUN_TEST(test_builtins_are_well_formed);
    return UNITY_END();
}
