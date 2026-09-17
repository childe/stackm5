#include <unity.h>

#include <cstring>

#include "jianpu.h"

void setUp(void)
{
}

void tearDown(void)
{
}

// 测试用的小助手：省掉每次手数字符串长度
static jianpu::Score P(const char *text)
{
    return jianpu::parse(text, std::strlen(text));
}

// 最基本的一条规则：一个光溜溜的数字 = 中音、一拍
void test_bare_digit_is_middle_octave_one_beat(void)
{
    jianpu::Score s = P("1");

    TEST_ASSERT_TRUE(s.error.ok);
    TEST_ASSERT_EQUAL_size_t(1, s.notes.size());
    TEST_ASSERT_EQUAL_UINT8(1, s.notes[0].step);
    TEST_ASSERT_EQUAL_INT8(0, s.notes[0].octave);
    TEST_ASSERT_EQUAL_INT8(0, s.notes[0].accidental);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, s.notes[0].beats);
}

// 0 是休止符：照样占一拍，只是不发声
void test_zero_is_a_rest(void)
{
    jianpu::Score s = P("1 0 2");

    TEST_ASSERT_TRUE(s.error.ok);
    TEST_ASSERT_EQUAL_size_t(3, s.notes.size());
    TEST_ASSERT_EQUAL_UINT8(0, s.notes[1].step);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, s.notes[1].beats);
}

// 八度点：' 往上、, 往下，都可以叠加
void test_octave_marks_shift_up_and_down(void)
{
    jianpu::Score s = P("1' 1'' 1, 1,,");

    TEST_ASSERT_TRUE(s.error.ok);
    TEST_ASSERT_EQUAL_size_t(4, s.notes.size());
    TEST_ASSERT_EQUAL_INT8(1, s.notes[0].octave);
    TEST_ASSERT_EQUAL_INT8(2, s.notes[1].octave);
    TEST_ASSERT_EQUAL_INT8(-1, s.notes[2].octave);
    TEST_ASSERT_EQUAL_INT8(-2, s.notes[3].octave);
}

// 减时线 / ：每加一条，时长减半（八分音符、十六分音符）
void test_slash_halves_the_duration(void)
{
    jianpu::Score s = P("1 1/ 1//");

    TEST_ASSERT_TRUE(s.error.ok);
    TEST_ASSERT_EQUAL_size_t(3, s.notes.size());
    TEST_ASSERT_EQUAL_FLOAT(1.0f, s.notes[0].beats);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, s.notes[1].beats);
    TEST_ASSERT_EQUAL_FLOAT(0.25f, s.notes[2].beats);
}

// 附点 . ：把音符自己的时长 ×1.5（所以附点八分 = 0.75 拍）
void test_dot_multiplies_duration_by_one_and_half(void)
{
    jianpu::Score s = P("1. 1/.");

    TEST_ASSERT_TRUE(s.error.ok);
    TEST_ASSERT_EQUAL_size_t(2, s.notes.size());
    TEST_ASSERT_EQUAL_FLOAT(1.5f, s.notes[0].beats);
    TEST_ASSERT_EQUAL_FLOAT(0.75f, s.notes[1].beats);
}

// 增时线 - 是独立 token，把「前一个」音符延长一拍。
// 所以 "3 - - - 5" 是 2 个音符（一个 4 拍、一个 1 拍），不是 5 个。
void test_dash_extends_previous_note(void)
{
    jianpu::Score s = P("3 - - - 5");

    TEST_ASSERT_TRUE(s.error.ok);
    TEST_ASSERT_EQUAL_size_t(2, s.notes.size());
    TEST_ASSERT_EQUAL_UINT8(3, s.notes[0].step);
    TEST_ASSERT_EQUAL_FLOAT(4.0f, s.notes[0].beats);
    TEST_ASSERT_EQUAL_UINT8(5, s.notes[1].step);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, s.notes[1].beats);
}

// 升降号写在数字「前面」：# 升半音、b 降半音，且不会影响后续音符
void test_accidental_prefix(void)
{
    jianpu::Score s = P("#4 b7 4");

    TEST_ASSERT_TRUE(s.error.ok);
    TEST_ASSERT_EQUAL_size_t(3, s.notes.size());
    TEST_ASSERT_EQUAL_UINT8(4, s.notes[0].step);
    TEST_ASSERT_EQUAL_INT8(1, s.notes[0].accidental);
    TEST_ASSERT_EQUAL_UINT8(7, s.notes[1].step);
    TEST_ASSERT_EQUAL_INT8(-1, s.notes[1].accidental);
    TEST_ASSERT_EQUAL_INT8(0, s.notes[2].accidental);
}

// srcPos/srcLen 要覆盖「整个音符 token」：前缀的升降号算进去，
// 后面跟着的增时线也算进去。播放时靠它在原文里整块高亮当前音符。
//   下标: 0123456789
//   原文: #1' 3 - -
void test_src_range_covers_whole_token(void)
{
    jianpu::Score s = P("#1' 3 - -");

    TEST_ASSERT_TRUE(s.error.ok);
    TEST_ASSERT_EQUAL_size_t(2, s.notes.size());
    TEST_ASSERT_EQUAL_UINT16(0, s.notes[0].srcPos);  // 从 '#' 开始
    TEST_ASSERT_EQUAL_UINT16(3, s.notes[0].srcLen);  // "#1'"
    TEST_ASSERT_EQUAL_UINT16(4, s.notes[1].srcPos);
    TEST_ASSERT_EQUAL_UINT16(5, s.notes[1].srcLen);  // "3 - -"
}

// 第一行是头部行：要读出调号和速度，而且绝不能被当成音符。
// 1=F 表示 1 唱作 F，F 比 C 高 5 个半音。拍号 3/4 解析但忽略。
void test_header_line_is_read_and_not_parsed_as_notes(void)
{
    jianpu::Score s = P("1=F 3/4 90\n3 3");

    TEST_ASSERT_TRUE(s.error.ok);
    TEST_ASSERT_EQUAL_INT8(5, s.header.keyRoot);
    TEST_ASSERT_EQUAL_INT(90, s.header.bpm);
    TEST_ASSERT_EQUAL_size_t(2, s.notes.size());
    TEST_ASSERT_EQUAL_UINT8(3, s.notes[0].step);
    TEST_ASSERT_EQUAL_UINT8(3, s.notes[1].step);
}

// 音级 → 频率。1=C 时中音 1 = C4 = 261.63Hz、5 = G4 = 392Hz，
// 高音 1 = C5 = 523.25Hz、低音 1 = C3 = 130.81Hz
void test_note_to_freq_in_c_major(void)
{
    jianpu::Score s = P("1 5 1' 1,");

    TEST_ASSERT_EQUAL_size_t(4, s.notes.size());
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 261.63f, jianpu::noteToFreq(s.notes[0], s.header));
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 392.00f, jianpu::noteToFreq(s.notes[1], s.header));
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 523.25f, jianpu::noteToFreq(s.notes[2], s.header));
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 130.81f, jianpu::noteToFreq(s.notes[3], s.header));
}

// 换调：1=F 时 1 还是唱 do，但实际发 F4 = 349.23Hz；
// 5 (sol) 就是 C5 = 523.25Hz。整首歌跟着搬，旋律不变。
void test_note_to_freq_transposes_with_key(void)
{
    jianpu::Score s = P("1=F 4/4 120\n1 5");

    TEST_ASSERT_EQUAL_size_t(2, s.notes.size());
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 349.23f, jianpu::noteToFreq(s.notes[0], s.header));
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 523.25f, jianpu::noteToFreq(s.notes[1], s.header));
}

// 休止符没有频率 —— 播放器靠这个判断该静音而不是发声
void test_rest_has_no_frequency(void)
{
    jianpu::Score s = P("0");

    TEST_ASSERT_EQUAL_size_t(1, s.notes.size());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, jianpu::noteToFreq(s.notes[0], s.header));
}

// totalBeats 是全曲总拍数，UI 底部靠它显示「多少拍 / 多少秒」。
// "3 3 3- 1/ 1/" = 1 + 1 + 2 + 0.5 + 0.5 = 5 拍
void test_total_beats_sums_all_notes(void)
{
    jianpu::Score s = P("3 3 3- 1/ 1/");

    TEST_ASSERT_EQUAL_size_t(5, s.notes.size());
    TEST_ASSERT_EQUAL_FLOAT(5.0f, s.totalBeats());
}

// 不认识的字符要报错，并精确指出是第几个字符 —— 编辑器靠这个把它标红
//   下标: 0123456
//   原文: 3 3 % 5
void test_unknown_character_is_reported_with_position(void)
{
    jianpu::Score s = P("3 3 % 5");

    TEST_ASSERT_FALSE(s.error.ok);
    TEST_ASSERT_EQUAL_UINT16(4, s.error.pos);
}

// 简谱只有 1-7（0 是休止符），8 和 9 要报专门的错而不是笼统的「不认识」
void test_digit_out_of_range_is_reported(void)
{
    jianpu::Score s = P("3 8");

    TEST_ASSERT_FALSE(s.error.ok);
    TEST_ASSERT_EQUAL_UINT16(2, s.error.pos);
    TEST_ASSERT_EQUAL_STRING("only 1-7 (0 = rest)", s.error.reason);
}

// 增时线是「延长前一个音符」，开头就出现说明没有可延长的对象
void test_leading_dash_is_an_error(void)
{
    jianpu::Score s = P("- 3");

    TEST_ASSERT_FALSE(s.error.ok);
    TEST_ASSERT_EQUAL_UINT16(0, s.error.pos);
}

// # 和 b 必须紧贴一个音级数字。"3 # 5" 绝不能悄悄把 5 升半音。
void test_accidental_must_be_followed_by_a_digit(void)
{
    jianpu::Score s = P("3 # 5");

    TEST_ASSERT_FALSE(s.error.ok);
    TEST_ASSERT_EQUAL_UINT16(2, s.error.pos);
}

// 调号写错要报错，不能悄悄当成 C 调（那样整首歌的音都是错的）
void test_bad_key_name_is_an_error(void)
{
    jianpu::Score s = P("1=H 4/4 120\n3");

    TEST_ASSERT_FALSE(s.error.ok);
    TEST_ASSERT_EQUAL_UINT16(2, s.error.pos);
}

// 分隔符要宽容：连续空格、换行、tab、小节线都只是分隔，不产生音符也不报错
void test_separators_are_tolerated(void)
{
    jianpu::Score s = P("3   3 | 3\n\t5 | ");

    TEST_ASSERT_TRUE(s.error.ok);
    TEST_ASSERT_EQUAL_size_t(4, s.notes.size());
}

// 不写头部行时用缺省值：1=C、120 BPM
void test_header_defaults_when_absent(void)
{
    jianpu::Score s = P("3 3");

    TEST_ASSERT_TRUE(s.error.ok);
    TEST_ASSERT_EQUAL_INT8(0, s.header.keyRoot);
    TEST_ASSERT_EQUAL_INT(120, s.header.bpm);
    TEST_ASSERT_EQUAL_size_t(2, s.notes.size());
}

// 所有修饰符叠在一起：升 + 高八度 + 减时线 + 附点
void test_all_modifiers_combined(void)
{
    jianpu::Score s = P("#1'/.");

    TEST_ASSERT_TRUE(s.error.ok);
    TEST_ASSERT_EQUAL_size_t(1, s.notes.size());
    TEST_ASSERT_EQUAL_UINT8(1, s.notes[0].step);
    TEST_ASSERT_EQUAL_INT8(1, s.notes[0].accidental);
    TEST_ASSERT_EQUAL_INT8(1, s.notes[0].octave);
    TEST_ASSERT_EQUAL_FLOAT(0.75f, s.notes[0].beats);
}

// 时间轴：120 BPM 下一拍 = 500ms。"3 3 3- 5" 的起始时刻应该是
// 0ms / 500ms / 1000ms（这个占 2 拍）/ 2000ms，全曲 2500ms。
void test_timeline_onsets_follow_bpm(void)
{
    jianpu::Score s = P("1=C 4/4 120\n3 3 3- 5");
    jianpu::Timeline t = jianpu::buildTimeline(s);

    TEST_ASSERT_EQUAL_size_t(4, t.onsetMs.size());
    TEST_ASSERT_EQUAL_UINT32(0, t.onsetMs[0]);
    TEST_ASSERT_EQUAL_UINT32(500, t.onsetMs[1]);
    TEST_ASSERT_EQUAL_UINT32(1000, t.onsetMs[2]);
    TEST_ASSERT_EQUAL_UINT32(2000, t.onsetMs[3]);
    TEST_ASSERT_EQUAL_UINT32(2500, t.totalMs);
}

// 速度翻倍，时间轴就减半
void test_timeline_scales_with_faster_bpm(void)
{
    jianpu::Score s = P("1=C 4/4 240\n3 3");
    jianpu::Timeline t = jianpu::buildTimeline(s);

    TEST_ASSERT_EQUAL_size_t(2, t.onsetMs.size());
    TEST_ASSERT_EQUAL_UINT32(0, t.onsetMs[0]);
    TEST_ASSERT_EQUAL_UINT32(250, t.onsetMs[1]);
    TEST_ASSERT_EQUAL_UINT32(500, t.totalMs);
}

// 发声只占 85% 时长，留间隙 —— 否则连续的 "3 3 3" 会粘成一个长音。
// 这是烟雾测试里听出来的，必须保住。
void test_timeline_leaves_a_gap_between_notes(void)
{
    jianpu::Score s = P("1=C 4/4 120\n3");
    jianpu::Timeline t = jianpu::buildTimeline(s);

    TEST_ASSERT_EQUAL_size_t(1, t.holdMs.size());
    TEST_ASSERT_EQUAL_UINT32(425, t.holdMs[0]);  // 500 的 85%
}

// indexAt：给一个流逝时间，答此刻该响第几个音；播完返回 -1
void test_index_at_walks_the_timeline_then_ends(void)
{
    jianpu::Score s = P("1=C 4/4 120\n3 3 5");  // 0-500 / 500-1000 / 1000-1500
    jianpu::Timeline t = jianpu::buildTimeline(s);

    TEST_ASSERT_EQUAL_INT(0, jianpu::indexAt(t, 0));
    TEST_ASSERT_EQUAL_INT(0, jianpu::indexAt(t, 499));
    TEST_ASSERT_EQUAL_INT(1, jianpu::indexAt(t, 500));
    TEST_ASSERT_EQUAL_INT(2, jianpu::indexAt(t, 1200));
    TEST_ASSERT_EQUAL_INT(-1, jianpu::indexAt(t, 1500));
    TEST_ASSERT_EQUAL_INT(-1, jianpu::indexAt(t, 99999));
}

// 切开头部行和音符正文。返回值含结尾那个换行，所以
// notes = text.substr(headerPrefixLen(...))
void test_header_prefix_len(void)
{
    // "1=C 4/4 120" 是 11 个字符，加上换行 = 12
    TEST_ASSERT_EQUAL_size_t(12, jianpu::headerPrefixLen("1=C 4/4 120\n3 3", 15));

    // 没有头部行
    TEST_ASSERT_EQUAL_size_t(0, jianpu::headerPrefixLen("3 3", 3));

    // 只有头部行、末尾没有换行
    TEST_ASSERT_EQUAL_size_t(11, jianpu::headerPrefixLen("1=C 4/4 120", 11));

    // 空文本
    TEST_ASSERT_EQUAL_size_t(0, jianpu::headerPrefixLen("", 0));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_header_prefix_len);
    RUN_TEST(test_timeline_onsets_follow_bpm);
    RUN_TEST(test_timeline_scales_with_faster_bpm);
    RUN_TEST(test_timeline_leaves_a_gap_between_notes);
    RUN_TEST(test_index_at_walks_the_timeline_then_ends);
    RUN_TEST(test_header_defaults_when_absent);
    RUN_TEST(test_all_modifiers_combined);
    RUN_TEST(test_digit_out_of_range_is_reported);
    RUN_TEST(test_leading_dash_is_an_error);
    RUN_TEST(test_accidental_must_be_followed_by_a_digit);
    RUN_TEST(test_bad_key_name_is_an_error);
    RUN_TEST(test_separators_are_tolerated);
    RUN_TEST(test_unknown_character_is_reported_with_position);
    RUN_TEST(test_total_beats_sums_all_notes);
    RUN_TEST(test_rest_has_no_frequency);
    RUN_TEST(test_note_to_freq_transposes_with_key);
    RUN_TEST(test_note_to_freq_in_c_major);
    RUN_TEST(test_header_line_is_read_and_not_parsed_as_notes);
    RUN_TEST(test_src_range_covers_whole_token);
    RUN_TEST(test_accidental_prefix);
    RUN_TEST(test_dash_extends_previous_note);
    RUN_TEST(test_dot_multiplies_duration_by_one_and_half);
    RUN_TEST(test_bare_digit_is_middle_octave_one_beat);
    RUN_TEST(test_zero_is_a_rest);
    RUN_TEST(test_octave_marks_shift_up_and_down);
    RUN_TEST(test_slash_halves_the_duration);
    return UNITY_END();
}
