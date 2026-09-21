#include <unity.h>

#include <cstring>
#include <string>

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

void test_pitch_to_bar_spans_three_octaves_and_clamps(void)
{
    TEST_ASSERT_EQUAL_INT(0, vizmodel::pitchToBar(130.81f, 24));    // C3 = 下界
    TEST_ASSERT_EQUAL_INT(8, vizmodel::pitchToBar(261.626f, 24));   // 中音 do
    TEST_ASSERT_EQUAL_INT(23, vizmodel::pitchToBar(1046.5f, 24));   // C6 = 上界

    // 越界钳制
    TEST_ASSERT_EQUAL_INT(0, vizmodel::pitchToBar(40.0f, 24));
    TEST_ASSERT_EQUAL_INT(23, vizmodel::pitchToBar(4000.0f, 24));

    // 休止符没有柱子
    TEST_ASSERT_EQUAL_INT(-1, vizmodel::pitchToBar(0.0f, 24));

    // 退化的柱数
    TEST_ASSERT_EQUAL_INT(-1, vizmodel::pitchToBar(440.0f, 0));
    TEST_ASSERT_EQUAL_INT(0, vizmodel::pitchToBar(440.0f, 1));
}

// 音高升高时柱下标不能往回走
void test_pitch_to_bar_is_monotonic(void)
{
    int prev = -1;
    for (float f = 100.0f; f < 1200.0f; f *= 1.03f) {
        const int b = vizmodel::pitchToBar(f, 24);
        TEST_ASSERT_TRUE(b >= prev);
        prev = b;
    }
}

void test_envelope_decays_within_the_hold(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, vizmodel::spectrumEnvelope(0, 400));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, vizmodel::kSpectrumTail, vizmodel::spectrumEnvelope(400, 400));

    // 超过时值就停在时值末尾的值上，不继续往下掉
    TEST_ASSERT_FLOAT_WITHIN(0.001f, vizmodel::kSpectrumTail, vizmodel::spectrumEnvelope(4000, 400));

    // 退化：零时值当作已经衰减到底（不除零）
    TEST_ASSERT_FLOAT_WITHIN(0.001f, vizmodel::kSpectrumTail, vizmodel::spectrumEnvelope(0, 0));

    // 时值内单调不增且不为负
    float prev = 2.0f;
    for (uint32_t t = 0; t <= 400; t += 20) {
        const float v = vizmodel::spectrumEnvelope(t, 400);
        TEST_ASSERT_TRUE(v <= prev + 0.0001f);
        TEST_ASSERT_TRUE(v >= 0.0f);
        prev = v;
    }
}

void test_bar_heights_peak_at_the_played_pitch(void)
{
    float h[24];
    vizmodel::barHeights(261.626f, 0, 400, h, 24);

    const int peak = vizmodel::pitchToBar(261.626f, 24);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, h[peak]);  // 起始时刻峰值最高

    for (int i = 0; i < 24; ++i) {
        TEST_ASSERT_TRUE(h[i] >= vizmodel::kNoiseFloor);  // 底噪线：静止时不全黑
        TEST_ASSERT_TRUE(h[i] <= 1.0f);
        if (i != peak) TEST_ASSERT_TRUE(h[i] < h[peak]);
    }

    // 邻柱按距离衰减
    TEST_ASSERT_TRUE(h[peak - 1] > h[peak - 2]);
    TEST_ASSERT_TRUE(h[peak + 1] > h[peak + 2]);
}

// 休止符 = 无激励：柱子衰减回底噪
void test_bar_heights_rest_leaves_only_the_noise_floor(void)
{
    float h[24];
    vizmodel::barHeights(0.0f, 0, 400, h, 24);

    for (int i = 0; i < 24; ++i) {
        TEST_ASSERT_FLOAT_WITHIN(0.0001f, vizmodel::kNoiseFloor, h[i]);
    }
}

void test_bar_heights_guards_degenerate_sizes(void)
{
    float one[1] = {-1.0f};
    vizmodel::barHeights(261.626f, 0, 400, one, 1);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, one[0]);

    // n <= 0 时一个字节都不许写
    float canary[2] = {-7.0f, -7.0f};
    vizmodel::barHeights(261.626f, 0, 400, canary, 0);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -7.0f, canary[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -7.0f, canary[1]);

    vizmodel::barHeights(261.626f, 0, 400, nullptr, 24);  // 不崩
}

void test_roll_now_x_sits_at_one_third(void)
{
    vizmodel::RollGeom g;  // 默认 2s 过去 + 4s 未来
    TEST_ASSERT_EQUAL_INT(80, vizmodel::rollNowX(g));

    g.pastMs = 0;
    TEST_ASSERT_EQUAL_INT(0, vizmodel::rollNowX(g));

    g.pastMs = 3000;
    g.futureMs = 3000;
    TEST_ASSERT_EQUAL_INT(120, vizmodel::rollNowX(g));

    g.pastMs = 0;  // 窗口为 0：不除零，退回左边界
    g.futureMs = 0;
    TEST_ASSERT_EQUAL_INT(0, vizmodel::rollNowX(g));
}

void test_roll_block_y_normalizes_the_pitch_range(void)
{
    const vizmodel::RollGeom g;  // y0=18 h=110 blockH=6
    const vizmodel::SpanSemi span = {0, 7};

    TEST_ASSERT_EQUAL_INT(18, vizmodel::rollBlockY(7, span, g));    // 最高音贴顶
    TEST_ASSERT_EQUAL_INT(122, vizmodel::rollBlockY(0, span, g));   // 最低音贴底

    // 越界钳制
    TEST_ASSERT_EQUAL_INT(18, vizmodel::rollBlockY(99, span, g));
    TEST_ASSERT_EQUAL_INT(122, vizmodel::rollBlockY(-99, span, g));

    // 音域只有一个半音（分母为 0）→ 固定画在效果区中线
    const vizmodel::SpanSemi flat = {3, 3};
    TEST_ASSERT_EQUAL_INT(70, vizmodel::rollBlockY(3, flat, g));
}

void test_roll_blocks_classify_past_now_future(void)
{
    const jianpu::Score s = S("1=C 4/4 120\n1 2 3 4 5");
    const jianpu::Timeline t = jianpu::buildTimeline(s);
    const vizmodel::RollGeom g;
    const vizmodel::SpanSemi span = vizmodel::scoreSemitoneSpan(s);

    vizmodel::RollBlock buf[16];
    const int n = vizmodel::rollBlocks(s, t, 1000, g, span, buf, 16);
    TEST_ASSERT_EQUAL_INT(5, n);  // 按时间顺序填充，一屏装得下这 5 个

    // 第 0 个音（0..425ms）已经放完，整块在竖线左边
    TEST_ASSERT_EQUAL_INT(vizmodel::RollState::Past, buf[0].state);
    TEST_ASSERT_EQUAL_INT(40, buf[0].x0);
    TEST_ASSERT_EQUAL_INT(57, buf[0].x1);
    TEST_ASSERT_TRUE(buf[0].x1 < vizmodel::rollNowX(g));

    // 第 2 个音正在响，左边缘正好压在竖线上
    TEST_ASSERT_EQUAL_INT(vizmodel::RollState::Now, buf[2].state);
    TEST_ASSERT_EQUAL_INT(80, buf[2].x0);
    TEST_ASSERT_EQUAL_INT(97, buf[2].x1);

    // 第 4 个音还没到，在竖线右边
    TEST_ASSERT_EQUAL_INT(vizmodel::RollState::Future, buf[4].state);
    TEST_ASSERT_TRUE(buf[4].x0 > vizmodel::rollNowX(g));

    // x 全在效果区内
    for (int i = 0; i < n; ++i) {
        TEST_ASSERT_TRUE(buf[i].x0 >= g.x0);
        TEST_ASSERT_TRUE(buf[i].x1 <= g.x0 + g.w - 1);
        TEST_ASSERT_TRUE(buf[i].x1 >= buf[i].x0);
    }
}

// 状态边界：elapsed == onset 算「正在响」，elapsed == onset+hold 算「已播」
void test_roll_blocks_state_boundaries(void)
{
    const jianpu::Score s = S("1=C 4/4 120\n1 2 3 4 5");
    const jianpu::Timeline t = jianpu::buildTimeline(s);
    const vizmodel::RollGeom g;
    const vizmodel::SpanSemi span = vizmodel::scoreSemitoneSpan(s);
    vizmodel::RollBlock buf[16];

    vizmodel::rollBlocks(s, t, 999, g, span, buf, 16);
    TEST_ASSERT_EQUAL_INT(vizmodel::RollState::Future, buf[2].state);

    vizmodel::rollBlocks(s, t, 1000, g, span, buf, 16);
    TEST_ASSERT_EQUAL_INT(vizmodel::RollState::Now, buf[2].state);

    vizmodel::rollBlocks(s, t, 1424, g, span, buf, 16);
    TEST_ASSERT_EQUAL_INT(vizmodel::RollState::Now, buf[2].state);

    vizmodel::rollBlocks(s, t, 1425, g, span, buf, 16);
    TEST_ASSERT_EQUAL_INT(vizmodel::RollState::Past, buf[2].state);
}

// 方块从右往左流过竖线
void test_roll_blocks_scroll_leftwards(void)
{
    const jianpu::Score s = S("1=C 4/4 120\n1 2 3 4 5");
    const jianpu::Timeline t = jianpu::buildTimeline(s);
    const vizmodel::RollGeom g;
    const vizmodel::SpanSemi span = vizmodel::scoreSemitoneSpan(s);

    vizmodel::RollBlock early[16], late[16];
    vizmodel::rollBlocks(s, t, 1000, g, span, early, 16);
    vizmodel::rollBlocks(s, t, 1200, g, span, late, 16);

    TEST_ASSERT_TRUE(late[0].x0 < early[0].x0);
    TEST_ASSERT_EQUAL_INT(32, late[0].x0);
}

// 纵轴：音高越高 y 越小
void test_roll_blocks_put_high_notes_higher(void)
{
    const jianpu::Score s = S("1=C 4/4 120\n1 5'");
    const jianpu::Timeline t = jianpu::buildTimeline(s);
    const vizmodel::RollGeom g;
    const vizmodel::SpanSemi span = vizmodel::scoreSemitoneSpan(s);

    vizmodel::RollBlock buf[8];
    const int n = vizmodel::rollBlocks(s, t, 0, g, span, buf, 8);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_TRUE(buf[1].y < buf[0].y);
}

// 休止符留空：不产生方块
void test_roll_blocks_skip_rests(void)
{
    const jianpu::Score s = S("1=C 4/4 120\n0 0 0");
    const jianpu::Timeline t = jianpu::buildTimeline(s);
    const vizmodel::RollGeom g;

    vizmodel::RollBlock buf[8];
    TEST_ASSERT_EQUAL_INT(0, vizmodel::rollBlocks(s, t, 0, g, {0, 0}, buf, 8));
}

// 滚出窗口的音不填；跨左边界的音被裁到边界内
void test_roll_blocks_window_filters_and_clips(void)
{
    const jianpu::Score s = S("1=C 4/4 120\n1 2 3 4 5");
    const jianpu::Timeline t = jianpu::buildTimeline(s);
    const vizmodel::RollGeom g;
    const vizmodel::SpanSemi span = vizmodel::scoreSemitoneSpan(s);
    vizmodel::RollBlock buf[16];

    // elapsed=2400：第 0 个音只剩个尾巴贴在左边界
    const int n = vizmodel::rollBlocks(s, t, 2400, g, span, buf, 16);
    TEST_ASSERT_TRUE(n >= 1);
    TEST_ASSERT_EQUAL_INT(0, buf[0].x0);

    // elapsed=2500：第 0 个音已经整块滚出去了，第一个方块换成后面的音
    vizmodel::rollBlocks(s, t, 2500, g, span, buf, 16);
    TEST_ASSERT_TRUE(buf[0].x1 > 1);
}

// 超过 cap 时只填 cap 个，一个字节都不许越界写
void test_roll_blocks_respect_the_capacity(void)
{
    const jianpu::Score s = S("1=C 4/4 300\n1 2 3 4 5 6 7 1' 2' 3' 4' 5'");
    const jianpu::Timeline t = jianpu::buildTimeline(s);
    const vizmodel::RollGeom g;
    const vizmodel::SpanSemi span = vizmodel::scoreSemitoneSpan(s);

    vizmodel::RollBlock buf[4];
    buf[3].x0 = -12345;  // canary

    TEST_ASSERT_EQUAL_INT(3, vizmodel::rollBlocks(s, t, 0, g, span, buf, 3));
    TEST_ASSERT_EQUAL_INT(-12345, buf[3].x0);

    TEST_ASSERT_EQUAL_INT(0, vizmodel::rollBlocks(s, t, 0, g, span, buf, 0));
    TEST_ASSERT_EQUAL_INT(0, vizmodel::rollBlocks(s, t, 0, g, span, nullptr, 8));
}

// 窗口里的方块多过 cap：不能填满前 cap 个就收手。
// 300 BPM + 三条减时线（0.125 拍）= 25ms 一个音，6000ms 的窗口里正好 240 个方块，
// 而竖线左边（过去 2000ms）就有 80 个 —— 按时间顺序填 64 个槽会在竖线左边就填满，
// 正在响的音被整个挤掉、竖线右边一片空白。所以要保留以「现在」为中心的那一段。
void test_roll_blocks_center_the_window_when_over_capacity(void)
{
    std::string text = "1=C 4/4 300\n";
    for (int i = 0; i < 400; ++i) text += "1/// ";  // 400 个 25ms 的音 = 10000ms

    const jianpu::Score s = S(text.c_str());
    TEST_ASSERT_TRUE(s.error.ok);
    TEST_ASSERT_EQUAL_size_t(400, s.notes.size());

    const jianpu::Timeline t = jianpu::buildTimeline(s);
    const vizmodel::RollGeom g;
    const vizmodel::SpanSemi span = vizmodel::scoreSemitoneSpan(s);

    vizmodel::RollBlock buf[65];
    buf[64].x0 = -12345;  // canary：一个字节都不许越界写

    const int n = vizmodel::rollBlocks(s, t, 5000, g, span, buf, 64);
    TEST_ASSERT_EQUAL_INT(64, n);
    TEST_ASSERT_EQUAL_INT(-12345, buf[64].x0);

    // 正在响的那个音（onset == 5000）一定在缓冲里，而且只有它是 Now
    int nows = 0;
    for (int i = 0; i < n; ++i) {
        if (buf[i].state == vizmodel::RollState::Now) ++nows;
    }
    TEST_ASSERT_EQUAL_INT(1, nows);

    // 竖线两侧都得有方块：留白落在窗口左右两端，不是把未来那一半整片吞掉
    const int nowX = vizmodel::rollNowX(g);
    int left = 0, right = 0;
    for (int i = 0; i < n; ++i) {
        if (buf[i].x1 < nowX) ++left;
        if (buf[i].x0 > nowX) ++right;
    }
    TEST_ASSERT_TRUE(left > 0);
    TEST_ASSERT_TRUE(right > 0);

    // 保留的一段仍然按时间递增，且不越出效果区
    for (int i = 0; i < n; ++i) {
        TEST_ASSERT_TRUE(buf[i].x0 >= g.x0);
        TEST_ASSERT_TRUE(buf[i].x1 <= g.x0 + g.w - 1);
        if (i > 0) TEST_ASSERT_TRUE(buf[i].x0 >= buf[i - 1].x0);
    }

    // cap 给到效果区宽度（240 = 一个方块至少 1px 时一屏的上限）时一个都不丢
    static vizmodel::RollBlock wide[240];
    TEST_ASSERT_EQUAL_INT(240, vizmodel::rollBlocks(s, t, 5000, g, span, wide, 240));
    TEST_ASSERT_EQUAL_INT(0, wide[0].x0);
    TEST_ASSERT_EQUAL_INT(239, wide[239].x1);
}

// 只有「屏上周期数」随音高变：高音密、低音疏
void test_wave_cycles_rise_with_pitch_and_clamp(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.01f, vizmodel::kWaveMinCycles,
                             vizmodel::waveCyclesOnScreen(130.81f));  // C3
    TEST_ASSERT_FLOAT_WITHIN(0.01f, vizmodel::kWaveMaxCycles,
                             vizmodel::waveCyclesOnScreen(1046.5f));  // C6
    TEST_ASSERT_TRUE(vizmodel::waveCyclesOnScreen(523.25f) >
                     vizmodel::waveCyclesOnScreen(261.626f));

    // 越界钳制在可读范围内
    TEST_ASSERT_FLOAT_WITHIN(0.01f, vizmodel::kWaveMinCycles, vizmodel::waveCyclesOnScreen(30.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, vizmodel::kWaveMaxCycles,
                             vizmodel::waveCyclesOnScreen(6000.0f));

    // 休止符也要给一个可用的值（画平线时这个值仍会代进公式）
    TEST_ASSERT_FLOAT_WITHIN(0.01f, vizmodel::kWaveMinCycles, vizmodel::waveCyclesOnScreen(0.0f));

    for (float f = 60.0f; f < 4000.0f; f *= 1.05f) {
        const float c = vizmodel::waveCyclesOnScreen(f);
        TEST_ASSERT_TRUE(c >= vizmodel::kWaveMinCycles);
        TEST_ASSERT_TRUE(c <= vizmodel::kWaveMaxCycles);
    }
}

void test_wave_amplitude_fades_to_forty_percent(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, vizmodel::waveAmplitude(0, 500));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.7f, vizmodel::waveAmplitude(250, 500));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, vizmodel::kWaveTailAmp, vizmodel::waveAmplitude(500, 500));

    // 超过时值不继续往下掉；退化的时值当作已衰减到底
    TEST_ASSERT_FLOAT_WITHIN(0.001f, vizmodel::kWaveTailAmp, vizmodel::waveAmplitude(5000, 500));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, vizmodel::kWaveTailAmp, vizmodel::waveAmplitude(0, 0));

    float prev = 2.0f;
    for (uint32_t t = 0; t <= 500; t += 25) {
        const float a = vizmodel::waveAmplitude(t, 500);
        TEST_ASSERT_TRUE(a <= prev + 0.0001f);                    // 单调不增
        TEST_ASSERT_TRUE(a >= vizmodel::kWaveTailAmp - 0.0001f);  // 不低于 40%
        prev = a;
    }
}

// 相位按固定角速度随 elapsed 匀速滚，与频率无关。
// 写成 2π·f·t 的话换音时 f 跳变会让相位整体跳一大截，
// 和「换音不跳变起点、只变密度」自相矛盾。
void test_wave_phase_rolls_at_a_fixed_rate(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, vizmodel::wavePhase(0));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.14159f, vizmodel::wavePhase(500));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, vizmodel::wavePhase(1000));  // 整周期回绕
    TEST_ASSERT_FLOAT_WITHIN(0.001f, vizmodel::wavePhase(500), vizmodel::wavePhase(1500));

    for (uint32_t t = 0; t < 3000; t += 7) {
        const float ph = vizmodel::wavePhase(t);
        TEST_ASSERT_TRUE(ph >= 0.0f);
        TEST_ASSERT_TRUE(ph < 6.2832f);
    }

    // 相邻毫秒之间只走一小步 —— 不管这一毫秒有没有换音，相位都是连续的
    for (uint32_t t = 1; t < 999; ++t) {
        const float d = vizmodel::wavePhase(t) - vizmodel::wavePhase(t - 1);
        TEST_ASSERT_TRUE(d > 0.0f);
        TEST_ASSERT_TRUE(d < 0.01f);
    }
}

// 拍 = 60000/bpm 毫秒（四分音符）。这里只用一定拿得到的 bpm，
// 不碰小节 / 拍号 —— jianpu::Header 里没有那个数据。
void test_beat_phase_wraps_every_beat(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, vizmodel::beatPhase(0, 120));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, vizmodel::beatPhase(250, 120));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, vizmodel::beatPhase(500, 120));  // 回绕
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, vizmodel::beatPhase(1250, 120));

    // settings 里的极端 BPM（20~300）都不溢出、不除零
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, vizmodel::beatPhase(1500, 20));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, vizmodel::beatPhase(100, 300));

    // 非法 bpm 退回 120，不除零
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, vizmodel::beatPhase(250, 0));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, vizmodel::beatPhase(250, -7));

    for (uint32_t t = 0; t < 4000; t += 13) {
        const float ph = vizmodel::beatPhase(t, 137);
        TEST_ASSERT_TRUE(ph >= 0.0f);
        TEST_ASSERT_TRUE(ph < 1.0f);
    }
}

// 亮度呼吸：拍首最亮、拍内衰减（字号不动，位图字号只能整数倍会跳）
void test_beat_level_breathes_from_bright_to_dim(void)
{
    TEST_ASSERT_EQUAL_INT(0, vizmodel::beatLevel(0.0f));
    TEST_ASSERT_EQUAL_INT(0, vizmodel::beatLevel(0.3f));
    TEST_ASSERT_EQUAL_INT(1, vizmodel::beatLevel(0.4f));
    TEST_ASSERT_EQUAL_INT(1, vizmodel::beatLevel(0.6f));
    TEST_ASSERT_EQUAL_INT(2, vizmodel::beatLevel(0.7f));
    TEST_ASSERT_EQUAL_INT(2, vizmodel::beatLevel(0.99f));

    // 越界钳制
    TEST_ASSERT_EQUAL_INT(0, vizmodel::beatLevel(-0.5f));
    TEST_ASSERT_EQUAL_INT(2, vizmodel::beatLevel(1.5f));
}

void test_note_glyph_reads_digit_and_octave(void)
{
    const jianpu::Score s = S("1=C 4/4 120\n3 5' 1,, 0'");

    const vizmodel::NoteGlyph a = vizmodel::noteGlyphAt(s, 0);
    TEST_ASSERT_TRUE(a.valid);
    TEST_ASSERT_EQUAL_CHAR('3', a.digit);
    TEST_ASSERT_EQUAL_INT8(0, a.octave);

    const vizmodel::NoteGlyph b = vizmodel::noteGlyphAt(s, 1);
    TEST_ASSERT_EQUAL_CHAR('5', b.digit);
    TEST_ASSERT_EQUAL_INT8(1, b.octave);

    const vizmodel::NoteGlyph c = vizmodel::noteGlyphAt(s, 2);
    TEST_ASSERT_EQUAL_CHAR('1', c.digit);
    TEST_ASSERT_EQUAL_INT8(-2, c.octave);

    // 休止符只显示 0，不画八度点
    const vizmodel::NoteGlyph rest = vizmodel::noteGlyphAt(s, 3);
    TEST_ASSERT_TRUE(rest.valid);
    TEST_ASSERT_EQUAL_CHAR('0', rest.digit);
    TEST_ASSERT_EQUAL_INT8(0, rest.octave);
}

// 前一个 / 后一个音符的下标会越界，越界必须是 invalid（调用方据此不画）
void test_note_glyph_out_of_range_is_invalid(void)
{
    const jianpu::Score s = S("1=C 4/4 120\n3");

    TEST_ASSERT_FALSE(vizmodel::noteGlyphAt(s, -1).valid);
    TEST_ASSERT_FALSE(vizmodel::noteGlyphAt(s, 1).valid);
    TEST_ASSERT_FALSE(vizmodel::noteGlyphAt(S("1=C 4/4 120\n"), 0).valid);
}

// resume 时把 _startMs 往前挪，使 now - _startMs 恰好等于冻结的 elapsed
void test_resume_start_keeps_the_frozen_elapsed(void)
{
    const uint32_t start = vizmodel::resumeStartMs(10000, 3000);
    TEST_ASSERT_EQUAL_UINT32(7000, start);
    TEST_ASSERT_EQUAL_UINT32(3000, 10000 - start);

    // now 小于 pausedElapsed（millis() 回绕过）：无符号回绕照样给出正确的差值
    const uint32_t wrapped = vizmodel::resumeStartMs(100, 500);
    TEST_ASSERT_EQUAL_UINT32(500, static_cast<uint32_t>(100 - wrapped));

    // 刚开始就暂停
    TEST_ASSERT_EQUAL_UINT32(1234, vizmodel::resumeStartMs(1234, 0));
}

// 暂停在发声段中途 → 剩下 onset+hold-elapsed，恢复时补这么长
void test_remaining_hold_in_the_middle_of_a_note(void)
{
    const uint32_t rest = vizmodel::remainingHoldMs(1200, 1000, 425);
    TEST_ASSERT_EQUAL_UINT32(225, rest);
    TEST_ASSERT_TRUE(rest > 0);
    TEST_ASSERT_TRUE(rest <= 425);
}

// 暂停恰好落在 15% 静音间隔里（onset+hold <= elapsed < onset+dur）→ 不补发
void test_remaining_hold_inside_the_silent_gap(void)
{
    TEST_ASSERT_EQUAL_UINT32(0, vizmodel::remainingHoldMs(1450, 1000, 425));
    TEST_ASSERT_EQUAL_UINT32(0, vizmodel::remainingHoldMs(1499, 1000, 425));
}

// 边界：elapsed == onset → 整段；elapsed == onset+hold → 0。不许 off-by-one
void test_remaining_hold_at_the_note_boundaries(void)
{
    TEST_ASSERT_EQUAL_UINT32(425, vizmodel::remainingHoldMs(1000, 1000, 425));
    TEST_ASSERT_EQUAL_UINT32(0, vizmodel::remainingHoldMs(1425, 1000, 425));
    TEST_ASSERT_EQUAL_UINT32(1, vizmodel::remainingHoldMs(1424, 1000, 425));

    // 已经过了这个音很久
    TEST_ASSERT_EQUAL_UINT32(0, vizmodel::remainingHoldMs(99999, 1000, 425));

    // 还没进这个音（理论上不会发生）：整段都还在，不做负数回绕
    TEST_ASSERT_EQUAL_UINT32(425, vizmodel::remainingHoldMs(900, 1000, 425));

    // 零时值
    TEST_ASSERT_EQUAL_UINT32(0, vizmodel::remainingHoldMs(1000, 1000, 0));
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
    RUN_TEST(test_pitch_to_bar_spans_three_octaves_and_clamps);
    RUN_TEST(test_pitch_to_bar_is_monotonic);
    RUN_TEST(test_envelope_decays_within_the_hold);
    RUN_TEST(test_bar_heights_peak_at_the_played_pitch);
    RUN_TEST(test_bar_heights_rest_leaves_only_the_noise_floor);
    RUN_TEST(test_bar_heights_guards_degenerate_sizes);
    RUN_TEST(test_roll_now_x_sits_at_one_third);
    RUN_TEST(test_roll_block_y_normalizes_the_pitch_range);
    RUN_TEST(test_roll_blocks_classify_past_now_future);
    RUN_TEST(test_roll_blocks_state_boundaries);
    RUN_TEST(test_roll_blocks_scroll_leftwards);
    RUN_TEST(test_roll_blocks_put_high_notes_higher);
    RUN_TEST(test_roll_blocks_skip_rests);
    RUN_TEST(test_roll_blocks_window_filters_and_clips);
    RUN_TEST(test_roll_blocks_respect_the_capacity);
    RUN_TEST(test_roll_blocks_center_the_window_when_over_capacity);
    RUN_TEST(test_wave_cycles_rise_with_pitch_and_clamp);
    RUN_TEST(test_wave_amplitude_fades_to_forty_percent);
    RUN_TEST(test_wave_phase_rolls_at_a_fixed_rate);
    RUN_TEST(test_beat_phase_wraps_every_beat);
    RUN_TEST(test_beat_level_breathes_from_bright_to_dim);
    RUN_TEST(test_note_glyph_reads_digit_and_octave);
    RUN_TEST(test_note_glyph_out_of_range_is_invalid);
    RUN_TEST(test_resume_start_keeps_the_frozen_elapsed);
    RUN_TEST(test_remaining_hold_in_the_middle_of_a_note);
    RUN_TEST(test_remaining_hold_inside_the_silent_gap);
    RUN_TEST(test_remaining_hold_at_the_note_boundaries);
    return UNITY_END();
}
