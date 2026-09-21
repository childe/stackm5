#include <unity.h>

#include <cstring>

#include "jianpu.h"
#include "vizmodel.h"

void setUp(void)
{
}

void tearDown(void)
{
}

// RGB565 的近似亮度，只用来断言四级配色一级比一级暗
static int luma565(uint16_t c)
{
    const int r8 = ((c >> 11) & 0x1F) << 3;
    const int g8 = ((c >> 5) & 0x3F) << 2;
    const int b8 = (c & 0x1F) << 3;
    return (r8 * 2 + g8 * 5 + b8) / 8;
}

// 测试用的小助手：省掉每次手数字符串长度
static jianpu::Score S(const char *text)
{
    return jianpu::parse(text, std::strlen(text));
}

// 四套调色板，每套内部一级比一级暗 —— 四种风格靠这个档位差表达强弱
void test_each_palette_is_monotonically_darker(void)
{
    for (int i = 0; i < vizmodel::kPaletteCount; ++i) {
        const vizmodel::Palette &p = vizmodel::kPalettes[i];
        TEST_ASSERT_TRUE(luma565(p.bright) > luma565(p.mid));
        TEST_ASSERT_TRUE(luma565(p.mid) > luma565(p.dim));
        TEST_ASSERT_TRUE(luma565(p.dim) > luma565(p.faint));
    }
}

// 正好 4 套，且互不相同（换了配色要看得出来）
void test_palettes_are_four_and_pairwise_distinct(void)
{
    TEST_ASSERT_EQUAL_INT(4, vizmodel::kPaletteCount);

    for (int i = 0; i < vizmodel::kPaletteCount; ++i) {
        for (int j = i + 1; j < vizmodel::kPaletteCount; ++j) {
            const bool differs = vizmodel::kPalettes[i].bright != vizmodel::kPalettes[j].bright ||
                                 vizmodel::kPalettes[i].mid != vizmodel::kPalettes[j].mid;
            TEST_ASSERT_TRUE(differs);
        }
    }
}

void test_color_at_maps_levels_and_clamps(void)
{
    const vizmodel::Palette &p = vizmodel::kPalettes[0];

    TEST_ASSERT_EQUAL_HEX16(p.bright, vizmodel::colorAt(p, 0));
    TEST_ASSERT_EQUAL_HEX16(p.mid, vizmodel::colorAt(p, 1));
    TEST_ASSERT_EQUAL_HEX16(p.dim, vizmodel::colorAt(p, 2));
    TEST_ASSERT_EQUAL_HEX16(p.faint, vizmodel::colorAt(p, 3));

    // 越界钳制：调用方算出的档位不小心越界时不能读到数组外
    TEST_ASSERT_EQUAL_HEX16(p.bright, vizmodel::colorAt(p, -5));
    TEST_ASSERT_EQUAL_HEX16(p.faint, vizmodel::colorAt(p, 9));
}

void test_level_of_intensity_buckets(void)
{
    TEST_ASSERT_EQUAL_INT(0, vizmodel::levelOfIntensity(1.0f));
    TEST_ASSERT_EQUAL_INT(0, vizmodel::levelOfIntensity(0.66f));
    TEST_ASSERT_EQUAL_INT(1, vizmodel::levelOfIntensity(0.5f));
    TEST_ASSERT_EQUAL_INT(2, vizmodel::levelOfIntensity(0.2f));
    TEST_ASSERT_EQUAL_INT(3, vizmodel::levelOfIntensity(0.0f));

    TEST_ASSERT_EQUAL_INT(0, vizmodel::levelOfIntensity(5.0f));
    TEST_ASSERT_EQUAL_INT(3, vizmodel::levelOfIntensity(-1.0f));
}

void test_format_mmss(void)
{
    char buf[8];

    vizmodel::formatMmSs(0, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("00:00", buf);

    vizmodel::formatMmSs(61000, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("01:01", buf);

    // 不进位：1999ms 还是 1 秒
    vizmodel::formatMmSs(1999, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("00:01", buf);

    // 荒谬的值钳到 99:59，不把缓冲撑爆
    vizmodel::formatMmSs(4294967295u, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("99:59", buf);
}

void test_progress_width(void)
{
    TEST_ASSERT_EQUAL_INT(0, vizmodel::progressWidth(0, 1000, 240));
    TEST_ASSERT_EQUAL_INT(120, vizmodel::progressWidth(500, 1000, 240));
    TEST_ASSERT_EQUAL_INT(240, vizmodel::progressWidth(1000, 1000, 240));

    // 已播超过总时长（最后一帧的竞态）→ 钳到满格
    TEST_ASSERT_EQUAL_INT(240, vizmodel::progressWidth(9999, 1000, 240));

    // 零时长不除零
    TEST_ASSERT_EQUAL_INT(0, vizmodel::progressWidth(500, 0, 240));
}

void test_semitone_of_freq_is_relative_to_middle_c(void)
{
    TEST_ASSERT_EQUAL_INT(0, vizmodel::semitoneOfFreq(261.626f));    // 中音 do
    TEST_ASSERT_EQUAL_INT(12, vizmodel::semitoneOfFreq(523.25f));    // 高八度
    TEST_ASSERT_EQUAL_INT(-12, vizmodel::semitoneOfFreq(130.81f));   // 低八度
    TEST_ASSERT_EQUAL_INT(7, vizmodel::semitoneOfFreq(392.0f));      // G4

    // 休止符 / 非法频率
    TEST_ASSERT_EQUAL_INT(vizmodel::kNoSemi, vizmodel::semitoneOfFreq(0.0f));
    TEST_ASSERT_EQUAL_INT(vizmodel::kNoSemi, vizmodel::semitoneOfFreq(-5.0f));
}

void test_note_semitone_matches_the_scale(void)
{
    const jianpu::Score s = S("1=C 4/4 120\n1 3 5 1' 1, 0");
    TEST_ASSERT_TRUE(s.error.ok);
    TEST_ASSERT_EQUAL_size_t(6, s.notes.size());

    TEST_ASSERT_EQUAL_INT(0, vizmodel::noteSemitone(s.notes[0], s.header));    // do
    TEST_ASSERT_EQUAL_INT(4, vizmodel::noteSemitone(s.notes[1], s.header));    // mi
    TEST_ASSERT_EQUAL_INT(7, vizmodel::noteSemitone(s.notes[2], s.header));    // so
    TEST_ASSERT_EQUAL_INT(12, vizmodel::noteSemitone(s.notes[3], s.header));   // 高音 do
    TEST_ASSERT_EQUAL_INT(-12, vizmodel::noteSemitone(s.notes[4], s.header));  // 低音 do
    TEST_ASSERT_EQUAL_INT(vizmodel::kNoSemi, vizmodel::noteSemitone(s.notes[5], s.header));
}

void test_note_semitone_follows_the_key(void)
{
    const jianpu::Score s = S("1=D 4/4 120\n1");
    TEST_ASSERT_EQUAL_INT(2, vizmodel::noteSemitone(s.notes[0], s.header));
}

void test_score_span_covers_lowest_and_highest(void)
{
    const vizmodel::SpanSemi sp = vizmodel::scoreSemitoneSpan(S("1=C 4/4 120\n1 5' 3 1,"));
    TEST_ASSERT_EQUAL_INT(-12, sp.minSemi);  // 1,
    TEST_ASSERT_EQUAL_INT(19, sp.maxSemi);   // 5' = 7 + 12
}

void test_score_span_single_note_is_a_point(void)
{
    const vizmodel::SpanSemi sp = vizmodel::scoreSemitoneSpan(S("1=C 4/4 120\n3"));
    TEST_ASSERT_EQUAL_INT(4, sp.minSemi);
    TEST_ASSERT_EQUAL_INT(4, sp.maxSemi);
}

// 全是休止符的谱 / 空谱：不崩，退化成 {0,0}（卷帘会画在中线）
void test_score_span_degenerates_for_rests_and_empty(void)
{
    const vizmodel::SpanSemi rests = vizmodel::scoreSemitoneSpan(S("1=C 4/4 120\n0 0 0"));
    TEST_ASSERT_EQUAL_INT(0, rests.minSemi);
    TEST_ASSERT_EQUAL_INT(0, rests.maxSemi);

    const vizmodel::SpanSemi empty = vizmodel::scoreSemitoneSpan(S("1=C 4/4 120\n"));
    TEST_ASSERT_EQUAL_INT(0, empty.minSemi);
    TEST_ASSERT_EQUAL_INT(0, empty.maxSemi);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_each_palette_is_monotonically_darker);
    RUN_TEST(test_palettes_are_four_and_pairwise_distinct);
    RUN_TEST(test_color_at_maps_levels_and_clamps);
    RUN_TEST(test_level_of_intensity_buckets);
    RUN_TEST(test_format_mmss);
    RUN_TEST(test_progress_width);
    RUN_TEST(test_semitone_of_freq_is_relative_to_middle_c);
    RUN_TEST(test_note_semitone_matches_the_scale);
    RUN_TEST(test_note_semitone_follows_the_key);
    RUN_TEST(test_score_span_covers_lowest_and_highest);
    RUN_TEST(test_score_span_single_note_is_a_point);
    RUN_TEST(test_score_span_degenerates_for_rests_and_empty);
    return UNITY_END();
}
