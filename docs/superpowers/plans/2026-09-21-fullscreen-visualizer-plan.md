# 简谱播放器：全屏可视化 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 曲库页按 `SPC` 播放成功后切进一个全屏可视化页，四种由谱面数据驱动的风格 + 四套单色配色可单键循环，支持暂停 / 恢复补音 / 自然放完自动退出。

**Architecture:** 所有「给定输入必出同样输出」的计算放进新的 `lib/vizmodel/`（零硬件依赖，Mac 上 native 单测全覆盖）；`Player` 加一个 `(_playing, _paused)` 三态机并把 `Score` / `Timeline` / 一次取齐的 `PlaybackFrame` 暴露成只读，成为可视化的**唯一数据源**；`src/viz_app.cpp` 只做「从 vizmodel 拿数、往画布画像素」，状态全封在文件内；`src/main.cpp` 只加一个 `Page::Viz` 和接线。

**Tech Stack:** C++17（`-std=gnu++17`）、PlatformIO、Arduino/M5Cardputer（LovyanGFX 绘图）、Unity（native 单测）。

**Spec:** `docs/superpowers/specs/2026-09-21-fullscreen-visualizer-design.md`

## Global Constraints

每个任务的要求都隐含包含这一节。值和措辞逐条抄自设计文档：

- **Player 是唯一数据源**：Viz 不自己存谱面、不自己维护第二条时间轴。要谱面一律走 `player.score()` / `player.timeline()`，要时间一律走 `player.frame()`。
- **全部纯函数**：`lib/vizmodel` 里的函数给定同样输入必须输出同样结果 —— 画面冻结（暂停）和单测都靠这一点。
- **`lib/` 零硬件依赖**：`lib/vizmodel` 只允许依赖 `lib/jianpu`，不得 include 任何 Arduino / M5 头文件（否则 native 测试链不上）。
- **每帧调用的函数不分配堆**：不返回 `std::vector`。调用方给固定容量数组、函数只填不分配（ESP32-S3FN8 无 PSRAM，每帧 30 次堆分配会持续搅动堆）。
- **风格 / 配色只存 RAM**，不落盘、不写闪存；本次开机内记住，重启归零。
- **切换风格 / 配色时不显示名字**（用户明确不要）。
- **不做小节线 / 小节内拍号相关的任何显示**。`jianpu::Header` 只有 `keyRoot` 和 `bpm`，拍号在解析器里被显式丢弃，"每小节几拍" 没有数据来源。只用 `bpm` 做拍点脉冲。
- **不采音频、不做 FFT**。
- **屏幕 240×135，布局固定**：`y 0..15` 顶栏、`y 18..127` 效果区、`y 130..134` 进度条。
- **编辑器页 `ENTER` 播放维持原样**，不进可视化。
- **休止符（频率 ≤ 0）= 无激励**：频谱柱衰减回底噪、波形变平线、卷帘留空、大字显示 `0`。
- 提交信息随 `git log` 的既有风格：中文短句，不加 `feat:` / `fix:` 之类前缀。
- 常用命令：`pio test -e native -f test_vizmodel`（跑本 feature 的单测）、`pio test -e native`（全部单测）、`pio run -e cardputer-adv`（编译）、`make flash`（烧写）。

## 文件结构

| 文件 | 职责 |
|---|---|
| `lib/vizmodel/vizmodel.h` / `.cpp`（新） | 全部纯逻辑：调色板、共用标量工具、音高换算、频谱柱、卷帘、示波器、拍点、大字简谱、播放时钟 |
| `test/test_vizmodel/test_main.cpp`（新） | 上面全部函数的 native 单测，逐任务追加 |
| `src/player.h` / `.cpp`（改） | 加 `(_playing, _paused)` 三态机、`pause()` / `resume()` + 恢复补音、只读快照 `frame()` / `score()` / `timeline()` |
| `src/viz_app.h` / `.cpp`（新） | 可视化页：顶栏 / 进度条 / 四种风格的绘制 + 页内按键；风格与配色的 RAM 状态封在 `.cpp` 内 |
| `src/main.cpp`（改） | `Page::Viz`、`playById` 改返回 `bool`、按键分发、30fps 置脏、自然放完自动回列表 |
| `README.md`（改） | 可视化页键位表、代码结构清单、新增的实测约束 |

任务顺序：先把 `lib/vizmodel` 一层层测出来（任务 1-7），再改 `Player`（任务 8），然后做出第一个能在设备上看见的切片（任务 9 = 骨架 + 频谱柱 + 接线），最后三种风格各一个任务（10-12），收尾是文档与上机验收（13）。

---

### Task 1: vizmodel 骨架与共用工具

**Files:**
- Create: `lib/vizmodel/vizmodel.h`
- Create: `lib/vizmodel/vizmodel.cpp`
- Test: `test/test_vizmodel/test_main.cpp`

**Interfaces:**
- Consumes: 无（本 feature 的第一个任务）
- Produces:
  - `struct vizmodel::Palette { uint16_t bright; uint16_t mid; uint16_t dim; uint16_t faint; };`
  - `constexpr int vizmodel::kPaletteCount = 4;` / `extern const Palette kPalettes[kPaletteCount];`
  - `uint16_t vizmodel::colorAt(const Palette &p, int level);`（0=bright … 3=faint，越界钳制）
  - `int vizmodel::levelOfIntensity(float v);`（0..1 强度 → 0..3 档位）
  - `void vizmodel::formatMmSs(uint32_t ms, char *out, size_t cap);`
  - `int vizmodel::progressWidth(uint32_t elapsedMs, uint32_t totalMs, int fullW);`

- [ ] **Step 1: 建立库骨架与测试文件，写下失败的测试**

创建 `lib/vizmodel/vizmodel.h`：

```cpp
// 全屏可视化的纯逻辑层 —— 零硬件依赖，可以在电脑上单元测试。
//
// 播放器是我们自己合成的音（谱面 → 频率 → 喇叭），不需要采集音频：每个音的
// 频率、起止时间、时值、BPM 拍点都能确定性地算出来。所以这里全是纯函数
// ——「给定同样输入必出同样结果」是硬要求，画面冻结（暂停）和单测都靠它。
//
// 设计见 docs/superpowers/specs/2026-09-21-fullscreen-visualizer-design.md
#pragma once

#include <jianpu.h>

#include <cstddef>
#include <cstdint>

namespace vizmodel {

// ── 配色 ────────────────────────────────────────────────────
// 一套 4 级亮度（RGB565），四种风格只从当前调色板取色，黑底。
// 单色系不会触发「高饱和多色混排刺眼」的问题（实机反馈只针对多色混排）。
struct Palette {
    uint16_t bright;
    uint16_t mid;
    uint16_t dim;
    uint16_t faint;
};

constexpr int kPaletteCount = 4;
extern const Palette kPalettes[kPaletteCount];

// level：0=bright 1=mid 2=dim 3=faint，越界钳制
uint16_t colorAt(const Palette &p, int level);

// 0..1 的强度 → 档位下标（0=bright … 3=faint）
int levelOfIntensity(float v);

// ── 顶栏 / 进度条的纯算术 ────────────────────────────────────
// "mm:ss"，最长 "99:59"（超过就钳住，不让格式串把缓冲撑爆）
void formatMmSs(uint32_t ms, char *out, size_t cap);

// 已播比例 → 前景条宽度。totalMs == 0 时返回 0（不除零）
int progressWidth(uint32_t elapsedMs, uint32_t totalMs, int fullW);

}  // namespace vizmodel
```

创建 `test/test_vizmodel/test_main.cpp`：

```cpp
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

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_each_palette_is_monotonically_darker);
    RUN_TEST(test_palettes_are_four_and_pairwise_distinct);
    RUN_TEST(test_color_at_maps_levels_and_clamps);
    RUN_TEST(test_level_of_intensity_buckets);
    RUN_TEST(test_format_mmss);
    RUN_TEST(test_progress_width);
    return UNITY_END();
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `pio test -e native -f test_vizmodel`
Expected: 链接失败 —— `undefined reference to vizmodel::kPalettes` 等（`vizmodel.cpp` 还不存在）

- [ ] **Step 3: 写实现**

创建 `lib/vizmodel/vizmodel.cpp`：

```cpp
#include "vizmodel.h"

#include <cstdio>

namespace vizmodel {

// 四套单色调色板（RGB565）。灰度那套用和背单词页一样的四个灰阶值。
const Palette kPalettes[kPaletteCount] = {
    {0xFFFF, 0xD69A, 0x9492, 0x738E},  // Gray：白→灰，与背单词一致
    {0x07FF, 0x0618, 0x0451, 0x02CB},  // Cyan：黑底青，示波器味
    {0xFDE0, 0xCCA0, 0x8B40, 0x5A00},  // Amber：黑底橙黄，老终端味
    {0x07E0, 0x0600, 0x0440, 0x02C0},  // Green：黑底绿，CRT 味
};

uint16_t colorAt(const Palette &p, int level)
{
    const int lv = (level < 0) ? 0 : ((level > 3) ? 3 : level);
    switch (lv) {
        case 0:
            return p.bright;
        case 1:
            return p.mid;
        case 2:
            return p.dim;
        default:
            return p.faint;
    }
}

int levelOfIntensity(float v)
{
    if (v >= 0.66f) return 0;
    if (v >= 0.33f) return 1;
    if (v >= 0.10f) return 2;
    return 3;
}

void formatMmSs(uint32_t ms, char *out, size_t cap)
{
    if (out == nullptr || cap == 0) return;

    const uint32_t total = ms / 1000;
    uint32_t mm = total / 60;
    uint32_t ss = total % 60;
    if (mm > 99) {  // 曲库里不会有这么长的曲子，但格式串的宽度得有保证
        mm = 99;
        ss = 59;
    }

    std::snprintf(out, cap, "%02u:%02u", static_cast<unsigned>(mm), static_cast<unsigned>(ss));
}

int progressWidth(uint32_t elapsedMs, uint32_t totalMs, int fullW)
{
    if (totalMs == 0 || fullW <= 0) return 0;
    if (elapsedMs > totalMs) elapsedMs = totalMs;

    // 先乘后除，用 64 位避免 240 * 几十万毫秒溢出
    return static_cast<int>(static_cast<uint64_t>(fullW) * elapsedMs / totalMs);
}

}  // namespace vizmodel
```

- [ ] **Step 4: 跑测试确认通过**

Run: `pio test -e native -f test_vizmodel`
Expected: 6 Tests 0 Failures 0 Ignored / PASSED

- [ ] **Step 5: 确认没有把旧测试弄坏**

Run: `pio test -e native`
Expected: 所有 suite（test_jianpu / test_remotemap / test_songs / test_texted / test_vocab / test_vizmodel）全 PASSED

- [ ] **Step 6: 提交**

```bash
git add lib/vizmodel test/test_vizmodel
git commit -m "可视化：vizmodel 骨架，四套单色调色板与顶栏/进度条算术"
```

---

### Task 2: 音高换算与整首谱的音域

**Files:**
- Modify: `lib/vizmodel/vizmodel.h`（在 `progressWidth` 声明之后追加）
- Modify: `lib/vizmodel/vizmodel.cpp`（在文件末尾 `}  // namespace vizmodel` 之前追加）
- Test: `test/test_vizmodel/test_main.cpp`

**Interfaces:**
- Consumes: Task 1 的 `vizmodel.h` / `.cpp` 已存在、测试文件已有 `setUp` / `tearDown` / `main()`
- Produces:
  - `constexpr float vizmodel::kMiddleCFreq = 261.626f;`（与 `jianpu::noteToFreq` 同一基准）
  - `constexpr int vizmodel::kNoSemi = -1000;`（休止符 / 无音高的哨兵值）
  - `int vizmodel::semitoneOfFreq(float freq);`
  - `int vizmodel::noteSemitone(const jianpu::Note &n, const jianpu::Header &h);`
  - `struct vizmodel::SpanSemi { int minSemi = 0; int maxSemi = 0; };`
  - `vizmodel::SpanSemi vizmodel::scoreSemitoneSpan(const jianpu::Score &s);`

- [ ] **Step 1: 写下失败的测试**

在 `test/test_vizmodel/test_main.cpp` 的 `luma565` 辅助函数之后加一个建谱助手：

```cpp
// 测试用的小助手：省掉每次手数字符串长度
static jianpu::Score S(const char *text)
{
    return jianpu::parse(text, std::strlen(text));
}
```

在 `main()` 之前追加这些测试：

```cpp
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
```

在 `main()` 里追加：

```cpp
    RUN_TEST(test_semitone_of_freq_is_relative_to_middle_c);
    RUN_TEST(test_note_semitone_matches_the_scale);
    RUN_TEST(test_note_semitone_follows_the_key);
    RUN_TEST(test_score_span_covers_lowest_and_highest);
    RUN_TEST(test_score_span_single_note_is_a_point);
    RUN_TEST(test_score_span_degenerates_for_rests_and_empty);
```

- [ ] **Step 2: 跑测试确认失败**

Run: `pio test -e native -f test_vizmodel`
Expected: 编译失败 —— `'semitoneOfFreq' is not a member of 'vizmodel'`

- [ ] **Step 3: 写实现**

在 `lib/vizmodel/vizmodel.h` 的 `progressWidth` 声明之后追加：

```cpp
// ── 音高 ────────────────────────────────────────────────────
// 半音数一律相对中音 do。基准和 jianpu::noteToFreq 用的是同一个值 ——
// 半音数不自己照抄音阶表，而是从频率反算，保证和发声永远一致。
constexpr float kMiddleCFreq = 261.626f;
constexpr int kNoSemi = -1000;  // 休止符 / 无音高

int semitoneOfFreq(float freq);
int noteSemitone(const jianpu::Note &n, const jianpu::Header &h);

// 整首谱的音域。卷帘的纵轴归一化要用，begin() 时算一次就够
struct SpanSemi {
    int minSemi = 0;
    int maxSemi = 0;
};

// 全是休止符 / 空谱 → {0, 0}
SpanSemi scoreSemitoneSpan(const jianpu::Score &s);
```

在 `lib/vizmodel/vizmodel.cpp` 里，先把头部的 include 补上 `<cmath>`：

```cpp
#include "vizmodel.h"

#include <cmath>
#include <cstdio>
```

然后在 `}  // namespace vizmodel` 之前追加：

```cpp
int semitoneOfFreq(float freq)
{
    if (freq <= 0.0f) return kNoSemi;
    return static_cast<int>(std::lround(12.0f * std::log2(freq / kMiddleCFreq)));
}

int noteSemitone(const jianpu::Note &n, const jianpu::Header &h)
{
    return semitoneOfFreq(jianpu::noteToFreq(n, h));
}

SpanSemi scoreSemitoneSpan(const jianpu::Score &s)
{
    SpanSemi span;
    bool any = false;

    for (const jianpu::Note &n : s.notes) {
        const int semi = noteSemitone(n, s.header);
        if (semi == kNoSemi) continue;  // 休止符不参与音域

        if (!any) {
            span.minSemi = semi;
            span.maxSemi = semi;
            any = true;
            continue;
        }
        if (semi < span.minSemi) span.minSemi = semi;
        if (semi > span.maxSemi) span.maxSemi = semi;
    }

    return span;
}
```

- [ ] **Step 4: 跑测试确认通过**

Run: `pio test -e native -f test_vizmodel`
Expected: 12 Tests 0 Failures 0 Ignored / PASSED

- [ ] **Step 5: 提交**

```bash
git add lib/vizmodel test/test_vizmodel
git commit -m "可视化：音高→半音数换算与整首谱的音域"
```

---

### Task 3: 频谱柱的数据

**Files:**
- Modify: `lib/vizmodel/vizmodel.h`（在 `scoreSemitoneSpan` 声明之后追加）
- Modify: `lib/vizmodel/vizmodel.cpp`（末尾追加）
- Test: `test/test_vizmodel/test_main.cpp`

**Interfaces:**
- Consumes: Task 2 的 `semitoneOfFreq` / `kNoSemi`
- Produces:
  - `constexpr int vizmodel::kBarCount = 24;`
  - `constexpr int vizmodel::kBarLowSemi = -12;` / `constexpr int vizmodel::kBarSemiSpan = 36;`（C3..C6，示波器也复用）
  - `constexpr float vizmodel::kNoiseFloor = 0.06f;` / `kNeighborFalloff = 0.45f;` / `kSpectrumTail = 0.15f;`
  - `int vizmodel::pitchToBar(float freq, int barCount);`（休止符 / `barCount <= 0` → -1）
  - `float vizmodel::spectrumEnvelope(uint32_t sinceOnsetMs, uint32_t holdMs);`
  - `void vizmodel::barHeights(float freq, uint32_t sinceOnsetMs, uint32_t holdMs, float *out, int n);`

- [ ] **Step 1: 写下失败的测试**

在 `test/test_vizmodel/test_main.cpp` 的 `main()` 之前追加：

```cpp
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
```

在 `main()` 里追加：

```cpp
    RUN_TEST(test_pitch_to_bar_spans_three_octaves_and_clamps);
    RUN_TEST(test_pitch_to_bar_is_monotonic);
    RUN_TEST(test_envelope_decays_within_the_hold);
    RUN_TEST(test_bar_heights_peak_at_the_played_pitch);
    RUN_TEST(test_bar_heights_rest_leaves_only_the_noise_floor);
    RUN_TEST(test_bar_heights_guards_degenerate_sizes);
```

- [ ] **Step 2: 跑测试确认失败**

Run: `pio test -e native -f test_vizmodel`
Expected: 编译失败 —— `'pitchToBar' is not a member of 'vizmodel'`

- [ ] **Step 3: 写实现**

在 `lib/vizmodel/vizmodel.h` 的 `scoreSemitoneSpan` 声明之后追加：

```cpp
// ── 风格 1：频谱柱 ──────────────────────────────────────────
constexpr int kBarCount = 24;      // 24 根柱铺满 240px
constexpr int kBarLowSemi = -12;   // 映射窗口下界 = C3（示波器也用这一对常量）
constexpr int kBarSemiSpan = 36;   // C3..C6，三个八度

constexpr float kNoiseFloor = 0.06f;       // 底噪线：静止时不全黑
constexpr float kNeighborFalloff = 0.45f;  // 每远一根柱乘这个
constexpr float kSpectrumTail = 0.15f;     // 时值末尾衰减到的比例

// 音高 → 柱下标。休止符或 barCount <= 0 返回 -1
int pitchToBar(float freq, int barCount);

// 时域包络：起始时刻 1.0，时值末尾 kSpectrumTail，指数衰减
float spectrumEnvelope(uint32_t sinceOnsetMs, uint32_t holdMs);

// 写 n 个 0..1 的柱高。休止符（freq <= 0）时全是底噪。
// 调用方给固定容量数组，这里只填不分配
void barHeights(float freq, uint32_t sinceOnsetMs, uint32_t holdMs, float *out, int n);
```

在 `lib/vizmodel/vizmodel.cpp` 末尾追加：

```cpp
int pitchToBar(float freq, int barCount)
{
    if (barCount <= 0) return -1;

    const int semi = semitoneOfFreq(freq);
    if (semi == kNoSemi) return -1;

    int idx = static_cast<int>(std::floor(static_cast<float>(semi - kBarLowSemi) *
                                          static_cast<float>(barCount) /
                                          static_cast<float>(kBarSemiSpan)));
    if (idx < 0) idx = 0;
    if (idx > barCount - 1) idx = barCount - 1;
    return idx;
}

float spectrumEnvelope(uint32_t sinceOnsetMs, uint32_t holdMs)
{
    if (holdMs == 0) return kSpectrumTail;  // 退化的时值：当作已经衰减到底

    const float t = (sinceOnsetMs >= holdMs)
                        ? 1.0f
                        : static_cast<float>(sinceOnsetMs) / static_cast<float>(holdMs);

    // pow(tail, t)：t=0 → 1，t=1 → tail，中间是指数曲线
    return std::pow(kSpectrumTail, t);
}

void barHeights(float freq, uint32_t sinceOnsetMs, uint32_t holdMs, float *out, int n)
{
    if (out == nullptr || n <= 0) return;

    for (int i = 0; i < n; ++i) out[i] = kNoiseFloor;

    const int bar = pitchToBar(freq, n);
    if (bar < 0) return;  // 休止符 = 无激励，只留底噪

    const float env = spectrumEnvelope(sinceOnsetMs, holdMs);
    for (int i = 0; i < n; ++i) {
        const int d = (i > bar) ? (i - bar) : (bar - i);
        const float v = env * std::pow(kNeighborFalloff, static_cast<float>(d));
        if (v > out[i]) out[i] = v;
    }
}
```

- [ ] **Step 4: 跑测试确认通过**

Run: `pio test -e native -f test_vizmodel`
Expected: 18 Tests 0 Failures 0 Ignored / PASSED

- [ ] **Step 5: 提交**

```bash
git add lib/vizmodel test/test_vizmodel
git commit -m "可视化：频谱柱的柱下标映射、时域包络与邻柱衰减"
```

---

### Task 4: 音高卷帘的数据

**Files:**
- Modify: `lib/vizmodel/vizmodel.h`（在 `barHeights` 声明之后追加）
- Modify: `lib/vizmodel/vizmodel.cpp`（末尾追加）
- Test: `test/test_vizmodel/test_main.cpp`

**Interfaces:**
- Consumes: Task 2 的 `noteSemitone` / `kNoSemi` / `SpanSemi`
- Produces:
  - `struct vizmodel::RollGeom { int x0 = 0; int y0 = 0; int w = 240; int h = 110; int blockH = 6; uint32_t pastMs = 2000; uint32_t futureMs = 4000; };`
  - `enum class vizmodel::RollState : uint8_t { Past, Now, Future };`
  - `struct vizmodel::RollBlock { int x0 = 0; int x1 = 0; int y = 0; RollState state = RollState::Future; };`
  - `int vizmodel::rollNowX(const RollGeom &g);`
  - `int vizmodel::rollBlockY(int semi, SpanSemi span, const RollGeom &g);`
  - `int vizmodel::rollBlocks(const jianpu::Score &s, const jianpu::Timeline &t, uint32_t elapsedMs, const RollGeom &g, SpanSemi span, RollBlock *out, int cap);` —— 返回写入的方块数（≤ cap），**按音符顺序填充**

- [ ] **Step 1: 写下失败的测试**

在 `test/test_vizmodel/test_main.cpp` 的 `main()` 之前追加。默认几何：`w=240 pastMs=2000 futureMs=4000` → 窗口 6000ms、0.04 px/ms、竖线在 x=80；测试用的谱 `"1=C 4/4 120\n1 2 3 4 5"` → 起始时刻 0/500/1000/1500/2000、每个音发声 425ms。

```cpp
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
    TEST_ASSERT_EQUAL_INT(5, n);  // 按音符顺序填充，一屏装得下这 5 个

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
```

在 `main()` 里追加：

```cpp
    RUN_TEST(test_roll_now_x_sits_at_one_third);
    RUN_TEST(test_roll_block_y_normalizes_the_pitch_range);
    RUN_TEST(test_roll_blocks_classify_past_now_future);
    RUN_TEST(test_roll_blocks_state_boundaries);
    RUN_TEST(test_roll_blocks_scroll_leftwards);
    RUN_TEST(test_roll_blocks_put_high_notes_higher);
    RUN_TEST(test_roll_blocks_skip_rests);
    RUN_TEST(test_roll_blocks_window_filters_and_clips);
    RUN_TEST(test_roll_blocks_respect_the_capacity);
```

- [ ] **Step 2: 跑测试确认失败**

Run: `pio test -e native -f test_vizmodel`
Expected: 编译失败 —— `'RollGeom' is not a member of 'vizmodel'`

- [ ] **Step 3: 写实现**

在 `lib/vizmodel/vizmodel.h` 的 `barHeights` 声明之后追加：

```cpp
// ── 风格 2：音高卷帘 ────────────────────────────────────────
// 横轴 = 时间窗口 [t-pastMs, t+futureMs]，纵轴 = 音高。
// 「现在」是一条固定的竖线，方块从右往左流过它。
// 竖线位置由 pastMs / futureMs 算出来（rollNowX），不单独给字段 ——
// 左右两半必须是同一个 px/ms，否则滚动速度会在竖线处突变。
struct RollGeom {
    int x0 = 0;   // 效果区左边界（含）
    int y0 = 18;  // 效果区上边界（含）
    int w = 240;
    int h = 110;
    int blockH = 6;
    uint32_t pastMs = 2000;
    uint32_t futureMs = 4000;
};

enum class RollState : uint8_t { Past, Now, Future };

struct RollBlock {
    int x0 = 0;
    int x1 = 0;  // 右边界（含）
    int y = 0;
    RollState state = RollState::Future;
};

int rollNowX(const RollGeom &g);

// 半音数 → 方块上边缘 y。音域退化（minSemi == maxSemi）时返回效果区中线
int rollBlockY(int semi, SpanSemi span, const RollGeom &g);

// 把落在时间窗口里的音符写成方块，返回写入的个数（≤ cap），按音符顺序填充。
// 休止符留空（不产生方块）。调用方给固定容量数组、这里只填不分配：
// 每帧 30 次返回 std::vector 会在无 PSRAM 的 ESP32 上持续搅动堆。
int rollBlocks(const jianpu::Score &s, const jianpu::Timeline &t, uint32_t elapsedMs,
               const RollGeom &g, SpanSemi span, RollBlock *out, int cap);
```

在 `lib/vizmodel/vizmodel.cpp` 末尾追加：

```cpp
int rollNowX(const RollGeom &g)
{
    const uint32_t window = g.pastMs + g.futureMs;
    if (window == 0) return g.x0;

    return g.x0 + static_cast<int>(std::lround(static_cast<double>(g.w) *
                                               static_cast<double>(g.pastMs) /
                                               static_cast<double>(window)));
}

int rollBlockY(int semi, SpanSemi span, const RollGeom &g)
{
    const int usable = g.h - g.blockH;
    if (span.maxSemi <= span.minSemi) {
        return g.y0 + usable / 2;  // 音域只有一个半音：固定画在中线
    }

    float t = static_cast<float>(span.maxSemi - semi) /
              static_cast<float>(span.maxSemi - span.minSemi);
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    return g.y0 + static_cast<int>(std::lround(t * static_cast<float>(usable)));
}

int rollBlocks(const jianpu::Score &s, const jianpu::Timeline &t, uint32_t elapsedMs,
               const RollGeom &g, SpanSemi span, RollBlock *out, int cap)
{
    if (out == nullptr || cap <= 0) return 0;

    const uint32_t window = g.pastMs + g.futureMs;
    if (window == 0 || g.w <= 0) return 0;

    const float pxPerMs = static_cast<float>(g.w) / static_cast<float>(window);
    const int rightEdge = g.x0 + g.w - 1;

    size_t count = s.notes.size();
    if (t.onsetMs.size() < count) count = t.onsetMs.size();
    if (t.holdMs.size() < count) count = t.holdMs.size();

    int n = 0;
    for (size_t i = 0; i < count && n < cap; ++i) {
        const int semi = noteSemitone(s.notes[i], s.header);
        if (semi == kNoSemi) continue;  // 休止符留空

        const uint32_t onset = t.onsetMs[i];
        const uint32_t hold = t.holdMs[i];

        // 全部换成 float 再减，避免无符号相减回绕成天文数字
        const float leftMs = static_cast<float>(onset) - static_cast<float>(elapsedMs) +
                             static_cast<float>(g.pastMs);
        const float rightMs = leftMs + static_cast<float>(hold);

        if (rightMs < 0.0f) continue;                        // 已经滚出左边
        if (leftMs > static_cast<float>(window)) continue;   // 还没进右边

        int bx0 = g.x0 + static_cast<int>(std::lround(leftMs * pxPerMs));
        int bx1 = g.x0 + static_cast<int>(std::lround(rightMs * pxPerMs));
        if (bx1 < bx0) bx1 = bx0;  // 极短的音至少占 1px
        if (bx0 < g.x0) bx0 = g.x0;
        if (bx1 > rightEdge) bx1 = rightEdge;
        if (bx1 < g.x0 || bx0 > rightEdge) continue;

        RollBlock b;
        b.x0 = bx0;
        b.x1 = bx1;
        b.y = rollBlockY(semi, span, g);
        if (elapsedMs < onset) {
            b.state = RollState::Future;
        } else if (elapsedMs - onset < hold) {
            b.state = RollState::Now;
        } else {
            b.state = RollState::Past;
        }

        out[n++] = b;
    }

    return n;
}
```

- [ ] **Step 4: 跑测试确认通过**

Run: `pio test -e native -f test_vizmodel`
Expected: 27 Tests 0 Failures 0 Ignored / PASSED

- [ ] **Step 5: 提交**

```bash
git add lib/vizmodel test/test_vizmodel
git commit -m "可视化：音高卷帘的时间窗口、音域归一化与固定容量方块缓冲"
```

---

### Task 5: 示波器的数据

**Files:**
- Modify: `lib/vizmodel/vizmodel.h`（在 `rollBlocks` 声明之后追加）
- Modify: `lib/vizmodel/vizmodel.cpp`（末尾追加）
- Test: `test/test_vizmodel/test_main.cpp`

**Interfaces:**
- Consumes: Task 2 的 `semitoneOfFreq` / `kNoSemi`、Task 3 的 `kBarLowSemi` / `kBarSemiSpan`
- Produces:
  - `constexpr float vizmodel::kWaveMinCycles = 1.5f;` / `kWaveMaxCycles = 12.0f;` / `kWaveTailAmp = 0.4f;`
  - `constexpr uint32_t vizmodel::kWavePhasePeriodMs = 1000;`
  - `float vizmodel::waveCyclesOnScreen(float freq);`
  - `float vizmodel::waveAmplitude(uint32_t sinceOnsetMs, uint32_t holdMs);`
  - `float vizmodel::wavePhase(uint32_t elapsedMs);`（弧度，**不吃 freq**）

- [ ] **Step 1: 写下失败的测试**

在 `test/test_vizmodel/test_main.cpp` 的 `main()` 之前追加：

```cpp
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
        TEST_ASSERT_TRUE(a <= prev + 0.0001f);                        // 单调不增
        TEST_ASSERT_TRUE(a >= vizmodel::kWaveTailAmp - 0.0001f);      // 不低于 40%
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
```

在 `main()` 里追加：

```cpp
    RUN_TEST(test_wave_cycles_rise_with_pitch_and_clamp);
    RUN_TEST(test_wave_amplitude_fades_to_forty_percent);
    RUN_TEST(test_wave_phase_rolls_at_a_fixed_rate);
```

- [ ] **Step 2: 跑测试确认失败**

Run: `pio test -e native -f test_vizmodel`
Expected: 编译失败 —— `'waveCyclesOnScreen' is not a member of 'vizmodel'`

- [ ] **Step 3: 写实现**

在 `lib/vizmodel/vizmodel.h` 的 `rollBlocks` 声明之后追加：

```cpp
// ── 风格 3：示波器 ─────────────────────────────────────────
// y(x) = A · sin(2π · cycles · x/W + phase)，三个量各管一件事：
//   cycles 只随音高变、A 只随音符时值内的衰减变、phase 只随 elapsed 匀速滚。
constexpr float kWaveMinCycles = 1.5f;
constexpr float kWaveMaxCycles = 12.0f;
constexpr float kWaveTailAmp = 0.4f;               // 时值末尾的振幅比例
constexpr uint32_t kWavePhasePeriodMs = 1000;      // 相位转一圈的毫秒数

// 屏上周期数：高音密、低音疏。映射窗口与频谱柱共用 kBarLowSemi/kBarSemiSpan。
// 休止符返回 kWaveMinCycles（平线由振幅那边负责）
float waveCyclesOnScreen(float freq);

// 振幅比例：起始 1.0，时值末尾 kWaveTailAmp
float waveAmplitude(uint32_t sinceOnsetMs, uint32_t holdMs);

// 相位（弧度）。固定角速度，**不吃 freq** —— 这是唯一能同时满足
// 「连续滚动」和「纯函数、可冻结」的写法
float wavePhase(uint32_t elapsedMs);
```

在 `lib/vizmodel/vizmodel.cpp` 末尾追加：

```cpp
float waveCyclesOnScreen(float freq)
{
    const int semi = semitoneOfFreq(freq);
    if (semi == kNoSemi) return kWaveMinCycles;

    const float t = static_cast<float>(semi - kBarLowSemi) / static_cast<float>(kBarSemiSpan);
    const float c = kWaveMinCycles + t * (kWaveMaxCycles - kWaveMinCycles);

    if (c < kWaveMinCycles) return kWaveMinCycles;
    if (c > kWaveMaxCycles) return kWaveMaxCycles;
    return c;
}

float waveAmplitude(uint32_t sinceOnsetMs, uint32_t holdMs)
{
    if (holdMs == 0) return kWaveTailAmp;

    const float t = (sinceOnsetMs >= holdMs)
                        ? 1.0f
                        : static_cast<float>(sinceOnsetMs) / static_cast<float>(holdMs);

    return 1.0f + t * (kWaveTailAmp - 1.0f);
}

float wavePhase(uint32_t elapsedMs)
{
    // 先整数取模再归一：elapsedMs 再大也不丢精度，而且严格周期
    const float t = static_cast<float>(elapsedMs % kWavePhasePeriodMs) /
                    static_cast<float>(kWavePhasePeriodMs);
    return t * 6.2831853f;
}
```

- [ ] **Step 4: 跑测试确认通过**

Run: `pio test -e native -f test_vizmodel`
Expected: 30 Tests 0 Failures 0 Ignored / PASSED

- [ ] **Step 5: 提交**

```bash
git add lib/vizmodel test/test_vizmodel
git commit -m "可视化：示波器的周期数、振幅衰减与与频率无关的滚动相位"
```

---

### Task 6: 拍点与大字简谱的数据

**Files:**
- Modify: `lib/vizmodel/vizmodel.h`（在 `wavePhase` 声明之后追加）
- Modify: `lib/vizmodel/vizmodel.cpp`（末尾追加）
- Test: `test/test_vizmodel/test_main.cpp`

**Interfaces:**
- Consumes: `jianpu::Score`
- Produces:
  - `float vizmodel::beatPhase(uint32_t elapsedMs, int bpm);`（[0,1)）
  - `int vizmodel::beatLevel(float phase);`（0=bright 1=mid 2=dim）
  - `struct vizmodel::NoteGlyph { bool valid = false; char digit = '0'; int8_t octave = 0; };`
  - `vizmodel::NoteGlyph vizmodel::noteGlyphAt(const jianpu::Score &s, int index);`

- [ ] **Step 1: 写下失败的测试**

在 `test/test_vizmodel/test_main.cpp` 的 `main()` 之前追加：

```cpp
// 拍 = 60000/bpm 毫秒（四分音符）。这里只用一定拿得到的 bpm，
// 不碰小节 / 拍号 —— jianpu::Header 里没有那个数据。
void test_beat_phase_wraps_every_beat(void)
{
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, vizmodel::beatPhase(0, 120));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, vizmodel::beatPhase(250, 120));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, vizmodel::beatPhase(500, 120));   // 回绕
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
```

在 `main()` 里追加：

```cpp
    RUN_TEST(test_beat_phase_wraps_every_beat);
    RUN_TEST(test_beat_level_breathes_from_bright_to_dim);
    RUN_TEST(test_note_glyph_reads_digit_and_octave);
    RUN_TEST(test_note_glyph_out_of_range_is_invalid);
```

- [ ] **Step 2: 跑测试确认失败**

Run: `pio test -e native -f test_vizmodel`
Expected: 编译失败 —— `'beatPhase' is not a member of 'vizmodel'`

- [ ] **Step 3: 写实现**

在 `lib/vizmodel/vizmodel.h` 的 `wavePhase` 声明之后追加：

```cpp
// ── 拍点 ────────────────────────────────────────────────────
// 一拍 = 60000/bpm 毫秒（四分音符）。只有这一个函数 —— 「每小节几拍」
// 在当前数据模型里没有来源（拍号被解析器显式丢弃），所以不做小节相关的显示。
// bpm <= 0 时退回 120。
float beatPhase(uint32_t elapsedMs, int bpm);

// 拍内相位 → 亮度档位（0=bright 1=mid 2=dim）。
// 拍首最亮、拍内衰减；字号不动 —— 位图字号只能整数倍，缩放会跳。
int beatLevel(float phase);

// ── 风格 4：大字简谱 ────────────────────────────────────────
// 简谱写法：数字 + 高低八度圆点。休止符是 0 且不画八度点。
struct NoteGlyph {
    bool valid = false;  // false = 没有这个音符（下标越界），调用方不画
    char digit = '0';
    int8_t octave = 0;  // 正 = 上点、负 = 下点
};

NoteGlyph noteGlyphAt(const jianpu::Score &s, int index);
```

在 `lib/vizmodel/vizmodel.cpp` 末尾追加：

```cpp
float beatPhase(uint32_t elapsedMs, int bpm)
{
    const int useBpm = (bpm > 0) ? bpm : 120;

    // 和 buildTimeline 用同一个 float 毫秒/拍，拍点才会和实际发声对齐
    const float msPerBeat = 60000.0f / static_cast<float>(useBpm);
    const float pos = std::fmod(static_cast<float>(elapsedMs), msPerBeat);

    return pos / msPerBeat;
}

int beatLevel(float phase)
{
    if (phase < 1.0f / 3.0f) return 0;  // 负数也落这里：拍首
    if (phase < 2.0f / 3.0f) return 1;
    return 2;
}

NoteGlyph noteGlyphAt(const jianpu::Score &s, int index)
{
    NoteGlyph g;
    if (index < 0 || static_cast<size_t>(index) >= s.notes.size()) return g;

    const jianpu::Note &n = s.notes[index];
    g.valid = true;
    g.digit = static_cast<char>('0' + ((n.step <= 7) ? n.step : 0));
    g.octave = (n.step == 0) ? 0 : n.octave;  // 休止符不画八度点

    return g;
}
```

- [ ] **Step 4: 跑测试确认通过**

Run: `pio test -e native -f test_vizmodel`
Expected: 34 Tests 0 Failures 0 Ignored / PASSED

- [ ] **Step 5: 提交**

```bash
git add lib/vizmodel test/test_vizmodel
git commit -m "可视化：拍点相位与亮度档位、大字简谱的写法提取"
```

---

### Task 7: 播放时钟（暂停 / 恢复的纯算术）

**Files:**
- Modify: `lib/vizmodel/vizmodel.h`（在 `noteGlyphAt` 声明之后追加）
- Modify: `lib/vizmodel/vizmodel.cpp`（末尾追加）
- Test: `test/test_vizmodel/test_main.cpp`

**Interfaces:**
- Consumes: 无
- Produces:
  - `uint32_t vizmodel::resumeStartMs(uint32_t nowMs, uint32_t pausedElapsedMs);`
  - `uint32_t vizmodel::remainingHoldMs(uint32_t elapsedMs, uint32_t onsetMs, uint32_t holdMs);`（0 的语义 = 恢复时不要补发这个音）

这两个函数放在 vizmodel 而不是再开一个 lib：就两个函数，且 vizmodel 已经是本 feature 唯一的「纯逻辑、Mac 上可测」落点。设计文档把这几条测试列为**必测，不是「若可抽离」** —— `Player` 那一层链不上 native，纯算术必须在这里兜住。

- [ ] **Step 1: 写下失败的测试**

在 `test/test_vizmodel/test_main.cpp` 的 `main()` 之前追加：

```cpp
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
```

在 `main()` 里追加：

```cpp
    RUN_TEST(test_resume_start_keeps_the_frozen_elapsed);
    RUN_TEST(test_remaining_hold_in_the_middle_of_a_note);
    RUN_TEST(test_remaining_hold_inside_the_silent_gap);
    RUN_TEST(test_remaining_hold_at_the_note_boundaries);
```

- [ ] **Step 2: 跑测试确认失败**

Run: `pio test -e native -f test_vizmodel`
Expected: 编译失败 —— `'resumeStartMs' is not a member of 'vizmodel'`

- [ ] **Step 3: 写实现**

在 `lib/vizmodel/vizmodel.h` 的 `noteGlyphAt` 声明之后追加：

```cpp
// ── 播放时钟（纯算术，Player 复用）────────────────────────────
// 恢复播放时的新 _startMs：使 now - _startMs 恰好等于冻结的 elapsed。
// 无符号回绕在这里是正确行为，不要加「防负数」的分支
uint32_t resumeStartMs(uint32_t nowMs, uint32_t pausedElapsedMs);

// 这个音还剩多少毫秒要发声。落在 15% 静音间隔里或已过该音则返回 0
// —— 0 的语义 = 恢复时不要补发这个音
uint32_t remainingHoldMs(uint32_t elapsedMs, uint32_t onsetMs, uint32_t holdMs);
```

在 `lib/vizmodel/vizmodel.cpp` 末尾追加：

```cpp
uint32_t resumeStartMs(uint32_t nowMs, uint32_t pausedElapsedMs)
{
    return nowMs - pausedElapsedMs;
}

uint32_t remainingHoldMs(uint32_t elapsedMs, uint32_t onsetMs, uint32_t holdMs)
{
    if (elapsedMs <= onsetMs) return holdMs;  // 还没进这个音：整段都还在

    const uint32_t gone = elapsedMs - onsetMs;
    if (gone >= holdMs) return 0;  // 落在静音间隔里 / 已过这个音

    return holdMs - gone;
}
```

- [ ] **Step 4: 跑测试确认通过**

Run: `pio test -e native -f test_vizmodel`
Expected: 38 Tests 0 Failures 0 Ignored / PASSED

- [ ] **Step 5: 跑全部单测**

Run: `pio test -e native`
Expected: 全部 suite PASSED

- [ ] **Step 6: 提交**

```bash
git add lib/vizmodel test/test_vizmodel
git commit -m "可视化：暂停恢复用的播放时钟算术（含静音间隔与无符号回绕）"
```

---

### Task 8: Player 的暂停状态机与只读快照

**Files:**
- Modify: `src/player.h`（整文件重写，见下）
- Modify: `src/player.cpp`（整文件重写，见下）

**Interfaces:**
- Consumes: Task 7 的 `vizmodel::resumeStartMs` / `vizmodel::remainingHoldMs`
- Produces:
  - `struct PlaybackFrame { bool playing; bool paused; uint32_t elapsedMs; uint32_t totalMs; int index; uint32_t onsetMs; uint32_t holdMs; float freq; };`（定义在 `src/player.h`）
  - `void Player::pause();` / `void Player::resume();` / `bool Player::isPaused() const;`
  - `PlaybackFrame Player::frame() const;`
  - `const jianpu::Score &Player::score() const;` / `const jianpu::Timeline &Player::timeline() const;`

**这一层没有 native 单测**：`Player` 直接 include `M5Cardputer.h`，native 环境链不上；为四个 bool 转移引一层虚接口不划算。纯算术已经被 Task 7 兜住，转移表和补音时机按 Step 3 的对账表逐条走查，实机验收在 Task 9 及之后。

- [ ] **Step 1: 重写 `src/player.h`**

```cpp
// 非阻塞播放器。
//
// 烟雾测试用的是 delay()，播放那十几秒整个设备锁死：按键无响应、不能中途停。
// 这里改成状态机：每次 loop() 调一次 update()，到点了才推进到下一个音，
// 主循环始终空着能读键盘。
//
// 「此刻该响第几个音」的计算在 lib/jianpu 的 Timeline 里（纯逻辑、有单元测试），
// 暂停 / 恢复的时间算术在 lib/vizmodel 里（同样纯逻辑、有单元测试），
// 这个类只负责把它们接到喇叭上。
//
// 播放状态由 (_playing, _paused) 表示，合法状态只有三个：
//   Stopped (false,false) 没挂曲子
//   Playing (true, false) 正常走时间轴
//   Paused  (true, true ) 挂着曲子但时间冻结
// (false,true) 是非法组合 —— start() 和 stop() 无条件清 _paused 来保证它
// 永不出现。漏了的话「暂停 → 停止 → 重新播另一首」会带着残留的 _paused
// 进新播放，update() 一进来就 return：喇叭全哑、画面冻在第一帧，而
// isPlaying() 还是 true，看起来像死机。
#pragma once

#include <jianpu.h>

// 一次取齐的只读播放快照。可视化页的唯一数据源 —— 分别调 elapsedMs() /
// currentIndex() 会跨 millis() 边界拿到不自洽的组合（index 已经是下一个音、
// elapsed 还是上一个音的）。
struct PlaybackFrame {
    bool playing = false;
    bool paused = false;
    uint32_t elapsedMs = 0;  // paused 时为冻结值
    uint32_t totalMs = 0;
    int index = -1;          // -1 = 没在播
    uint32_t onsetMs = 0;    // index >= 0 时有效
    uint32_t holdMs = 0;
    float freq = 0.0f;       // <= 0 表示休止符
};

class Player {
public:
    void start(const jianpu::Score &score);
    void stop();
    void pause();   // 只在 Playing 下生效
    void resume();  // 只在 Paused 下生效
    void update();  // 每次 loop() 调一次

    // 暂停时仍为 true：曲子还挂着，主循环不能误判成放完
    bool isPlaying() const
    {
        return _playing;
    }

    bool isPaused() const
    {
        return _paused;
    }

    // 当前正在响的音符下标，-1 = 没在播（UI 高亮用）
    int currentIndex() const;

    uint32_t elapsedMs() const;

    uint32_t totalMs() const
    {
        return _timeline.totalMs;
    }

    PlaybackFrame frame() const;

    // 谱面按值持有，生命周期覆盖整个播放过程，所以可以安全地暴露成只读。
    // 可视化页要谱面一律走这两个 —— 它不许自己再存一份、也不许自己维护
    // 第二条时间轴。
    const jianpu::Score &score() const
    {
        return _score;
    }

    const jianpu::Timeline &timeline() const
    {
        return _timeline;
    }

private:
    jianpu::Score _score;
    jianpu::Timeline _timeline;
    bool _playing = false;
    bool _paused = false;
    uint32_t _startMs = 0;
    uint32_t _pausedElapsed = 0;  // 暂停那一刻的 elapsed，冻结画面用
    int _index = -1;              // 已经触发过发声的音符
};
```

- [ ] **Step 2: 重写 `src/player.cpp`**

```cpp
#include "player.h"

#include <M5Cardputer.h>
#include <vizmodel.h>

void Player::start(const jianpu::Score &score)
{
    // 无条件清 _paused：上一首暂停着被 stop 掉又立刻播下一首时，
    // 残留的 _paused 会让 update() 一进来就 return（全哑 + 冻屏）
    _paused = false;

    _score = score;
    _timeline = jianpu::buildTimeline(_score);

    if (_score.notes.empty() || _timeline.totalMs == 0) {
        _playing = false;
        return;
    }

    _startMs = millis();
    _index = -1;
    _playing = true;

    update();  // 立刻触发第一个音，不等下一轮 loop
}

void Player::stop()
{
    _paused = false;  // 同 start()：非法组合 (false,true) 绝不能留下
    _playing = false;
    _index = -1;
    M5Cardputer.Speaker.stop();
}

void Player::pause()
{
    if (!_playing || _paused) return;  // Stopped / Paused 下是 no-op

    _pausedElapsed = millis() - _startMs;
    M5Cardputer.Speaker.stop();
    _paused = true;
}

void Player::resume()
{
    if (!_playing || !_paused) return;  // 其余状态下是 no-op

    _startMs = vizmodel::resumeStartMs(millis(), _pausedElapsed);
    _paused = false;

    // 补音：不补的话恢复后半个音是哑的，听起来像丢一拍。
    // 补完不动 _index —— update() 下一轮算出的 idx 仍等于 _index，
    // 会在「还在同一个音里」那一支提前 return，不会重复触发。
    if (_index >= 0 && static_cast<size_t>(_index) < _score.notes.size()) {
        const uint32_t rest = vizmodel::remainingHoldMs(_pausedElapsed, _timeline.onsetMs[_index],
                                                        _timeline.holdMs[_index]);
        const float freq = jianpu::noteToFreq(_score.notes[_index], _score.header);
        if (rest > 0 && freq > 0.0f) M5Cardputer.Speaker.tone(freq, rest);
    }
}

int Player::currentIndex() const
{
    if (!_playing) return -1;
    if (_paused) return jianpu::indexAt(_timeline, _pausedElapsed);  // 冻结，多次调用不漂移
    return _index;
}

uint32_t Player::elapsedMs() const
{
    if (!_playing) return 0;
    if (_paused) return _pausedElapsed;
    return millis() - _startMs;
}

PlaybackFrame Player::frame() const
{
    PlaybackFrame f;
    f.playing = _playing;
    f.paused = _paused;
    f.totalMs = _timeline.totalMs;
    if (!_playing) return f;

    // millis() 只取一次（elapsedMs() 内部），index 从同一个 elapsed 算出来
    f.elapsedMs = elapsedMs();
    f.index = jianpu::indexAt(_timeline, f.elapsedMs);

    if (f.index >= 0 && static_cast<size_t>(f.index) < _score.notes.size()) {
        f.onsetMs = _timeline.onsetMs[f.index];
        f.holdMs = _timeline.holdMs[f.index];
        f.freq = jianpu::noteToFreq(_score.notes[f.index], _score.header);
    }

    return f;
}

void Player::update()
{
    if (!_playing || _paused) return;  // 暂停期间不推进 _index、不碰喇叭

    const int idx = jianpu::indexAt(_timeline, millis() - _startMs);

    if (idx < 0) {  // 播完了
        stop();     // 于是 _paused 也被清掉
        return;
    }
    if (idx == _index) return;  // 还在同一个音里，什么都不用做

    _index = idx;

    const jianpu::Note &n = _score.notes[idx];
    const float freq = jianpu::noteToFreq(n, _score.header);

    if (freq > 0.0f) {
        // holdMs 已经是时长的 85%，留出的间隙让连续相同的音能分开听
        M5Cardputer.Speaker.tone(freq, _timeline.holdMs[idx]);
    } else {
        M5Cardputer.Speaker.stop();  // 休止符
    }
}
```

- [ ] **Step 3: 对着转移表逐条走查代码**

逐行对账（读代码确认，不改代码）：

| 转移 | 代码里必须做到 |
|---|---|
| `start()` | 无条件 `_paused = false` 在最前面；`_startMs = millis()`；`_index = -1`；`_playing = true`；末尾调 `update()`；空谱 / 零时长时 `_playing = false` 且 `_paused` 已是 false |
| `stop()` | 无条件 `_paused = false`；`_playing = false`；`_index = -1`；`Speaker.stop()` |
| `pause()` | `!_playing \|\| _paused` 时 no-op；记 `_pausedElapsed`；`Speaker.stop()`；置 `_paused` |
| `resume()` | `!_playing \|\| !_paused` 时 no-op；`_startMs = resumeStartMs(...)`；清 `_paused`；再补音；不动 `_index` |
| 自然放完 | `update()` 里 `idx < 0` → 走 `stop()` |

不变量：
- `!_playing → !_paused`：唯一给 `_paused` 置 true 的地方是 `pause()`，它先判了 `_playing`；两个清零点都无条件。
- Paused 期间 `elapsedMs()` 恒等于 `_pausedElapsed`、`currentIndex()` 恒定、`isPlaying()` 为 true、`update()` 直接返回。

- [ ] **Step 4: 编译确认设备侧能过**

Run: `pio run -e cardputer-adv`
Expected: SUCCESS（`src/player.cpp` include 了 `<vizmodel.h>`，LDF 会像 `<jianpu.h>` 一样把 `lib/vizmodel` 拉进来）

- [ ] **Step 5: 确认 native 单测没被弄坏**

Run: `pio test -e native`
Expected: 全部 PASSED（`src/` 不参与 native 测试构建，这一步只是防手滑）

- [ ] **Step 6: 提交**

```bash
git add src/player.h src/player.cpp
git commit -m "播放器：三态暂停状态机、恢复补音与一次取齐的只读快照"
```

---

### Task 9: 可视化页骨架 + 频谱柱 + main 接线

**Files:**
- Create: `src/viz_app.h`
- Create: `src/viz_app.cpp`
- Modify: `src/main.cpp`（8 处改动，见下）

**Interfaces:**
- Consumes: Task 8 的 `PlaybackFrame` / `Player::frame()` / `score()` / `pause()` / `resume()` / `isPaused()`；Task 1 的 `colorAt` / `levelOfIntensity` / `formatMmSs` / `progressWidth`；Task 2 的 `scoreSemitoneSpan`；Task 3 的 `barHeights` / `kBarCount`
- Produces:
  - `void viz_app::begin(const char *title, uint8_t id, const Player &player);`
  - `void viz_app::draw(LovyanGFX &g, const Player &player);`
  - `bool viz_app::handleKey(char c, Player &player);`（false = 要回曲库页）
  - `static bool playById(uint8_t id)`（main.cpp 内，改了返回类型）
  - `Page::Viz`
  - viz_app.cpp 内的 `enum class Style : uint8_t { Spectrum }` + `kStyleCount`，后面三个任务各追加一项

这一层没有 native 单测（LovyanGFX 链不上 native），验证 = 编译 + 上机目检。

- [ ] **Step 1: 建 `src/viz_app.h`**

```cpp
// 全屏可视化页。
//
// 曲库页按 SPC 播放成功就切到这一页，整屏画效果：
//   SPC 暂停 / 恢复   . 换风格   , 换配色   ` 停止并回列表
//
// 状态全封在 viz_app.cpp 里（和 vocab_app / remote_app 一样），main.cpp 只
// 需要知道「开始 / 画一帧 / 喂按键」这三件事。
//
// 不收 Score 参数：playById 里的 jianpu::Score 是局部变量，start() 之后就
// 出作用域了。要谱面一律走 player.score() / player.timeline()（Player 按值
// 持有），绝不持有指向调用方局部量的引用或指针。
#pragma once

#include <M5GFX.h>

#include <cstdint>

#include "player.h"

namespace viz_app {

// 进入这一页时调用。title 会被拷进内部固定缓冲 —— 调用方给的是
// gEntries[gSel].preview.c_str()，而 refreshEntries() 会整个重建 gEntries，
// std::string 的缓冲连带失效，存下这个指针就是悬垂。
// id 只用来显示曲号。
void begin(const char *title, uint8_t id, const Player &player);

void draw(LovyanGFX &g, const Player &player);

// 返回 false 表示要回曲库页（停止播放和切页由 main.cpp 做）
bool handleKey(char c, Player &player);

}  // namespace viz_app
```

- [ ] **Step 2: 建 `src/viz_app.cpp`**

```cpp
#include "viz_app.h"

#include <vizmodel.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// 屏幕旋转后 240x135。布局（设计文档第 2 节）：
//   y 0..15    顶栏：左 = 曲号 + 首行预览，右 = 已播/总时长（暂停时加 II）
//   y 18..127  效果区
//   y 130..134 进度条
constexpr int kScreenW = 240;
constexpr int kCharW = 8;
constexpr int kCols = kScreenW / kCharW;  // 30

constexpr int kTopY = 0;
constexpr int kFxY = 18;
constexpr int kFxH = 110;
constexpr int kProgY = 130;
constexpr int kProgH = 5;

// 风格。每加一种就往这里追加一项并把 kStyleCount 加一 —— `.` 键按它取模循环。
enum class Style : uint8_t { Spectrum };
constexpr int kStyleCount = 1;

// 风格和配色只活在 RAM 里：下次播放沿用，重启归零（设计明确不落盘）
Style gStyle = Style::Spectrum;
int gPaletteIdx = 0;

char gTitle[48] = {0};
uint8_t gId = 0;
vizmodel::SpanSemi gSpan;  // begin() 时算一次，卷帘风格用

const vizmodel::Palette &palette()
{
    return vizmodel::kPalettes[gPaletteIdx];
}

// 当前音已经响了多久。没在播（index < 0）时给 0
uint32_t sinceOnset(const PlaybackFrame &f)
{
    if (f.index < 0 || f.elapsedMs <= f.onsetMs) return 0;
    return f.elapsedMs - f.onsetMs;
}

void drawTopBar(LovyanGFX &g, const PlaybackFrame &f)
{
    const vizmodel::Palette &p = palette();

    char el[8], to[8];
    vizmodel::formatMmSs(f.elapsedMs, el, sizeof(el));
    vizmodel::formatMmSs(f.totalMs, to, sizeof(to));

    char right[24];
    std::snprintf(right, sizeof(right), "%s%s/%s", f.paused ? "II " : "", el, to);
    const int rightCols = static_cast<int>(std::strlen(right));

    g.setTextColor(p.mid, TFT_BLACK);
    g.drawString(right, kScreenW - rightCols * kCharW, kTopY);

    // 左边按剩下的列数截断，别和右边的时间叠在一起
    char left[64];
    std::snprintf(left, sizeof(left), "%02u %s", static_cast<unsigned>(gId), gTitle);
    int leftCols = kCols - rightCols - 1;
    if (leftCols < 0) leftCols = 0;
    if (static_cast<int>(std::strlen(left)) > leftCols) left[leftCols] = '\0';

    g.setTextColor(p.dim, TFT_BLACK);
    g.drawString(left, 0, kTopY);
}

void drawProgress(LovyanGFX &g, const PlaybackFrame &f)
{
    const vizmodel::Palette &p = palette();

    g.fillRect(0, kProgY, kScreenW, kProgH, p.faint);
    const int w = vizmodel::progressWidth(f.elapsedMs, f.totalMs, kScreenW);
    if (w > 0) g.fillRect(0, kProgY, w, kProgH, p.bright);
}

// ── 风格 1：频谱柱 ──────────────────────────────────────────
void drawSpectrum(LovyanGFX &g, const PlaybackFrame &f)
{
    const vizmodel::Palette &p = palette();

    float h[vizmodel::kBarCount];
    // 没在播时按休止符画（只剩底噪），不去碰 f.freq
    vizmodel::barHeights(f.index >= 0 ? f.freq : 0.0f, sinceOnset(f), f.holdMs, h,
                         vizmodel::kBarCount);

    const int bw = kScreenW / vizmodel::kBarCount;  // 24 根 x 10px 正好铺满
    for (int i = 0; i < vizmodel::kBarCount; ++i) {
        int barH = static_cast<int>(std::lround(h[i] * kFxH));
        if (barH < 1) barH = 1;  // 底噪线：静止时不全黑
        if (barH > kFxH) barH = kFxH;

        g.fillRect(i * bw, kFxY + kFxH - barH, bw - 1, barH,
                   vizmodel::colorAt(p, vizmodel::levelOfIntensity(h[i])));
    }
}

}  // namespace

void viz_app::begin(const char *title, uint8_t id, const Player &player)
{
    // 拷进自己的固定缓冲并截断。用 snprintf 而不是 strlcpy：效果一样
    // （截断 + 保证 NUL），但不依赖 BSD 扩展在两个工具链里都在
    std::snprintf(gTitle, sizeof(gTitle), "%s", title ? title : "");
    gId = id;

    // 音域在这里算一次：卷帘每帧重扫全谱是浪费
    gSpan = vizmodel::scoreSemitoneSpan(player.score());
}

void viz_app::draw(LovyanGFX &g, const Player &player)
{
    const PlaybackFrame f = player.frame();  // 一次取齐，整帧用同一个 elapsed

    drawTopBar(g, f);
    drawProgress(g, f);

    switch (gStyle) {
        case Style::Spectrum:
            drawSpectrum(g, f);
            break;
    }
}

bool viz_app::handleKey(char c, Player &player)
{
    switch (c) {
        case '`':
            return false;  // 回曲库页

        case ' ':
            player.isPaused() ? player.resume() : player.pause();
            break;

        case '.':
            gStyle = static_cast<Style>((static_cast<int>(gStyle) + 1) % kStyleCount);
            break;

        case ',':
            gPaletteIdx = (gPaletteIdx + 1) % vizmodel::kPaletteCount;
            break;

        default:
            break;  // 其余按键忽略
    }

    return true;
}
```

- [ ] **Step 3: 改 `src/main.cpp`（8 处）**

改动 1 —— 头部注释里「曲库页」那段之后、「编辑页」之前插入：

```
 * 可视化页（曲库页按空格播放后自动进入）
 *   空格         暂停 / 恢复
 *   .           切换风格（频谱柱 / 卷帘 / 示波器 / 大字简谱）
 *   ,           切换配色
 *   `           停止并回曲库页
 *
```

改动 2 —— include 区，在 `#include "remote_app.h"` 之后加一行：

```cpp
#include "viz_app.h"
```

改动 3 —— 常量区，在 `static constexpr uint32_t kPreviewMs = 140;` 之后加：

```cpp
// 可视化页的重绘间隔：约 30fps。画面靠 elapsed 连续变化，不等按键。
static constexpr uint32_t kVizFrameMs = 33;
```

改动 4 —— `Page` 枚举：

把
```cpp
enum class Page { Menu, Library, Editor, Settings, Vocab, Remote };
```
改成
```cpp
enum class Page { Menu, Library, Editor, Settings, Vocab, Remote, Viz };
```

改动 5 —— `playById` 改成返回 `bool`。把整个函数替换成：

```cpp
// 返回 true = 真的在播了；调用方靠它决定要不要切到可视化页。
// 判据必须是 start() 之后的 isPlaying()，不能只看解析守卫：Player::start()
// 自己还会在 totalMs == 0 时把 _playing 置回 false，只看守卫会漏掉这种，
// 结果切进可视化页面对一个没在播的 Player。
static bool playById(uint8_t id)
{
    gDirty = true;

    std::string text;
    if (!library::load(id, text)) return false;

    const jianpu::Score s = jianpu::parse(text.c_str(), text.size());
    if (!s.error.ok || s.notes.empty()) return false;

    gPlayingOwnScore = false;
    gPlayer.start(s);
    return gPlayer.isPlaying();
}
```

改动 6 —— `draw()` 的 switch 里，`case Page::Settings:` 之后加：

```cpp
        case Page::Viz:
            viz_app::draw(g, gPlayer);
            break;
```

改动 7 —— 按键：在 `handleLibraryKeys` 之前插入新函数，并改曲库页的空格分支、`handleKeys()` 的 switch。

新函数：

```cpp
static void handleVizKeys(const Keyboard_Class::KeysState &st)
{
    for (const char c : st.word) {
        if (!viz_app::handleKey(c, gPlayer)) {
            gPlayer.stop();
            gPage = Page::Library;
            refreshEntries();
        }
        gDirty = true;
    }
}
```

`handleLibraryKeys` 里把
```cpp
        } else if (c == ' ') {
            if (gPlayer.isPlaying()) {
                gPlayer.stop();
            } else if (!gEntries.empty()) {
                playById(gEntries[gSel].id);
            }
            gDirty = true;
```
改成
```cpp
        } else if (c == ' ') {
            if (gPlayer.isPlaying()) {
                gPlayer.stop();
            } else if (!gEntries.empty()) {
                const library::Entry &e = gEntries[gSel];
                // 播放失败（读盘 / 解析 / 空谱 / 零时长）就留在曲库页，
                // 行为与加可视化之前完全一致
                if (playById(e.id)) {
                    viz_app::begin(e.preview.c_str(), e.id, gPlayer);
                    gPage = Page::Viz;
                }
            }
            gDirty = true;
```

`handleKeys()` 的 switch 里，`case Page::Settings:` 之后加：

```cpp
        case Page::Viz:
            handleVizKeys(st);
            break;
```

改动 8 —— `loop()` 里，在遥控页那段 `if (gPage == Page::Remote) { ... }` 之后插入：

```cpp
    // 可视化页：画面靠 elapsed 连续变化，不等按键，所以按固定帧率置脏
    if (gPage == Page::Viz) {
        static uint32_t lastVizMs = 0;
        if (millis() - lastVizMs >= kVizFrameMs) {
            lastVizMs = millis();
            gDirty = true;
        }

        // 暂停期间 isPlaying() 仍为 true（曲子还挂着），所以这条只在
        // 自然放完时成立 —— 放完自动回曲库页
        if (!gPlayer.isPlaying() && !gPlayer.isPaused()) {
            gPage = Page::Library;
            refreshEntries();
            gDirty = true;
        }
    }
```

（自动存盘守卫 `const bool editing = (gPage == Page::Editor || gPage == Page::Settings);` 保持不动 —— 可视化页不碰谱面缓冲。）

- [ ] **Step 4: 编译**

Run: `pio run -e cardputer-adv`
Expected: SUCCESS

- [ ] **Step 5: 烧写并上机目检**

Run: `make flash`

逐条确认：
1. 菜单页按 `1` 进曲库 → 选一首 → `SPC`：立刻切到黑底全屏页，底部进度条从左往右走，顶栏右侧时间在跑（`mm:ss/mm:ss`），左侧是「曲号 + 谱面开头」。
2. 效果区有 24 根柱子；正在响的音那一带隆起成峰、随后回落；休止符处只剩一条底噪线，不全黑。
3. `SPC`：声音停、画面冻结（柱高和时间都不动）、时间左边出现 `II`；再按 `SPC`：接着响、画面继续（听不出丢拍）。
4. `,`：配色在 灰 → 青 → 橙黄 → 绿 → 灰 之间循环，顶栏 / 柱子 / 进度条一起变；不显示配色名字。
5. `.`：此刻只有一种风格，画面不变（正常）。
6. `` ` ``：回曲库页，声音停。
7. 让曲子自然放完：自动回曲库页。
8. 暂停中按 `` ` `` 回列表，立刻选另一首按 `SPC`：新曲子正常出声、画面正常走（验证 `_paused` 没残留）。
9. 播放中画面不闪（离屏画布 + 30fps）。

- [ ] **Step 6: 提交**

```bash
git add src/viz_app.h src/viz_app.cpp src/main.cpp
git commit -m "全屏可视化页：骨架、频谱柱风格与曲库页接线"
```

---

### Task 10: 音高卷帘风格

**Files:**
- Modify: `src/viz_app.cpp`（加 `kBlockCap` / `gBlocks` / `drawRoll`，扩 `Style` 与 `kStyleCount`，`draw()` 加一个 case）

**Interfaces:**
- Consumes: Task 4 的 `RollGeom` / `RollState` / `RollBlock` / `rollNowX` / `rollBlocks`；Task 9 的 `gSpan` / `palette()`；`player.score()` / `player.timeline()`
- Produces: `Style::Roll`，`kStyleCount == 2`

- [ ] **Step 1: 把风格表从 1 项扩到 2 项**

在 `src/viz_app.cpp` 里把
```cpp
enum class Style : uint8_t { Spectrum };
constexpr int kStyleCount = 1;
```
改成
```cpp
enum class Style : uint8_t { Spectrum, Roll };
constexpr int kStyleCount = 2;
```

在 `gSpan` 声明之后追加固定容量的方块缓冲：

```cpp
// 卷帘的方块缓冲：固定容量、不每帧分配（无 PSRAM 的 ESP32 上每帧 30 次
// 返回 std::vector 会持续搅动堆）。容量按一屏最多画得下的方块数给
constexpr int kBlockCap = 64;
vizmodel::RollBlock gBlocks[kBlockCap];
```

- [ ] **Step 2: 加 `drawRoll`**

在 `drawSpectrum` 之后、匿名 namespace 结束之前追加：

```cpp
// ── 风格 2：音高卷帘 ────────────────────────────────────────
void drawRoll(LovyanGFX &g, const Player &player, const PlaybackFrame &f)
{
    const vizmodel::Palette &p = palette();

    vizmodel::RollGeom geom;
    geom.x0 = 0;
    geom.y0 = kFxY;
    geom.w = kScreenW;
    geom.h = kFxH;

    // 「现在」是一条固定的竖线，方块从右往左流过它
    g.drawFastVLine(vizmodel::rollNowX(geom), kFxY, kFxH, p.faint);

    const int n = vizmodel::rollBlocks(player.score(), player.timeline(), f.elapsedMs, geom, gSpan,
                                       gBlocks, kBlockCap);
    for (int i = 0; i < n; ++i) {
        const vizmodel::RollBlock &b = gBlocks[i];

        uint16_t c = p.mid;  // 未播
        if (b.state == vizmodel::RollState::Now) {
            c = p.bright;
        } else if (b.state == vizmodel::RollState::Past) {
            c = p.dim;
        }

        g.fillRect(b.x0, b.y, b.x1 - b.x0 + 1, geom.blockH, c);
    }
}
```

- [ ] **Step 3: 接进 `draw()` 的 switch**

在 `case Style::Spectrum:` 那一支之后追加：

```cpp
        case Style::Roll:
            drawRoll(g, player, f);
            break;
```

- [ ] **Step 4: 编译**

Run: `pio run -e cardputer-adv`
Expected: SUCCESS

- [ ] **Step 5: 烧写并上机目检**

Run: `make flash`

1. `SPC` 播放 → `.` 一次切到卷帘：方块按音高排成横向流，从右往左滚过左 1/3 处的竖线。
2. 已播的方块暗、正在响的最亮、未播的中等亮度。
3. 高音方块在上、低音在下；休止符处留空。
4. `SPC` 暂停：方块停住不动；恢复后继续滚。
5. `,` 换配色：方块和竖线一起变色。
6. 选一首音域窄的（比如只有 `1 1 1`）：方块画在效果区中线，不崩。
7. `.` 再按一次回到频谱柱（两种风格循环）。

- [ ] **Step 6: 提交**

```bash
git add src/viz_app.cpp
git commit -m "可视化：音高卷帘风格"
```

---

### Task 11: 示波器风格

**Files:**
- Modify: `src/viz_app.cpp`（加 `drawWave`，扩 `Style` 与 `kStyleCount`，`draw()` 加一个 case）

**Interfaces:**
- Consumes: Task 5 的 `waveCyclesOnScreen` / `waveAmplitude` / `wavePhase`；Task 9 的 `sinceOnset` / `palette()`
- Produces: `Style::Wave`，`kStyleCount == 3`

- [ ] **Step 1: 把风格表从 2 项扩到 3 项**

把
```cpp
enum class Style : uint8_t { Spectrum, Roll };
constexpr int kStyleCount = 2;
```
改成
```cpp
enum class Style : uint8_t { Spectrum, Roll, Wave };
constexpr int kStyleCount = 3;
```

- [ ] **Step 2: 加 `drawWave`**

在 `drawRoll` 之后追加：

```cpp
// ── 风格 3：示波器 ─────────────────────────────────────────
// y(x) = A · sin(2π · cycles · x/W + phase)：只有 cycles 随音高变，
// A 在音符时值内衰减，phase 按固定角速度随 elapsed 匀速滚（与频率无关）
void drawWave(LovyanGFX &g, const PlaybackFrame &f)
{
    const vizmodel::Palette &p = palette();

    const int midY = kFxY + kFxH / 2;
    const float maxAmp = static_cast<float>(kFxH / 2 - 2);

    // 休止符 / 没在播 = 无激励 → 平线
    const bool excited = (f.index >= 0 && f.freq > 0.0f);
    const float amp = excited ? vizmodel::waveAmplitude(sinceOnset(f), f.holdMs) * maxAmp : 0.0f;
    const float cycles = vizmodel::waveCyclesOnScreen(excited ? f.freq : 0.0f);
    const float phase = vizmodel::wavePhase(f.elapsedMs);

    int prevY = midY;
    for (int x = 0; x < kScreenW; ++x) {
        const float a =
            6.2831853f * cycles * static_cast<float>(x) / static_cast<float>(kScreenW) + phase;
        const int y = midY - static_cast<int>(std::lround(amp * std::sin(a)));

        // 高音时相邻像素的 y 差得远，画点会断成虚线，所以逐段连线
        g.drawLine(x == 0 ? 0 : x - 1, prevY, x, y, p.bright);
        prevY = y;
    }
}
```

- [ ] **Step 3: 接进 `draw()` 的 switch**

在 `case Style::Roll:` 那一支之后追加：

```cpp
        case Style::Wave:
            drawWave(g, f);
            break;
```

- [ ] **Step 4: 编译**

Run: `pio run -e cardputer-adv`
Expected: SUCCESS

- [ ] **Step 5: 烧写并上机目检**

Run: `make flash`

1. `.` 切到示波器：一条横扫全屏的正弦波形，连续滚动不断线。
2. 高音时波形密、低音时疏；**换音的瞬间只有密度变，波形不整体跳一大截**（这是相位不吃频率的验证点）。
3. 一个音响下去振幅从满幅衰减到约 40%，不衰减到 0。
4. 休止符处变成一条平线。
5. `SPC` 暂停：波形完全冻住（滚动也停）；恢复后继续滚。
6. `,` 换配色：波形颜色跟着变。
7. 30fps 下不闪、不卡（240 个 `sin` 每帧对 ESP32-S3 是小钱）。

- [ ] **Step 6: 提交**

```bash
git add src/viz_app.cpp
git commit -m "可视化：示波器风格"
```

---

### Task 12: 大字简谱风格

**Files:**
- Modify: `src/viz_app.cpp`（加 `drawGlyph` / `drawBigNote`，扩 `Style` 与 `kStyleCount`，`draw()` 加一个 case）

**Interfaces:**
- Consumes: Task 6 的 `beatPhase` / `beatLevel` / `NoteGlyph` / `noteGlyphAt`；Task 1 的 `colorAt`；`player.score()`（取 `header.bpm`）
- Produces: `Style::BigNote`，`kStyleCount == 4`

- [ ] **Step 1: 把风格表从 3 项扩到 4 项**

把
```cpp
enum class Style : uint8_t { Spectrum, Roll, Wave };
constexpr int kStyleCount = 3;
```
改成
```cpp
enum class Style : uint8_t { Spectrum, Roll, Wave, BigNote };
constexpr int kStyleCount = 4;
```

- [ ] **Step 2: 加 `drawGlyph` 和 `drawBigNote`**

在 `drawWave` 之后追加：

```cpp
// ── 风格 4：大字简谱 ────────────────────────────────────────
// 数字 + 高低八度圆点，位图字体整数倍放大。cx 是水平中心
void drawGlyph(LovyanGFX &g, const vizmodel::NoteGlyph &gl, int cx, int y, int size,
               uint16_t color)
{
    if (!gl.valid) return;  // 下标越界（第一个音没有「前一个」）→ 不画

    const int w = kCharW * size;
    const int h = 16 * size;
    const int x = cx - w / 2;

    g.setFont(&fonts::AsciiFont8x16);
    g.setTextSize(static_cast<uint8_t>(size));
    g.setTextColor(color, TFT_BLACK);
    const char text[2] = {gl.digit, '\0'};
    g.drawString(text, x, y);
    g.setTextSize(1);  // 字号是全局状态，用完必须还原

    // 八度点：上方 = 高八度、下方 = 低八度，最多画两个
    const int dots = (gl.octave > 0) ? gl.octave : -gl.octave;
    const int r = (size >= 4) ? 3 : 2;
    for (int i = 0; i < dots && i < 2; ++i) {
        const int dy = (gl.octave > 0) ? y - (r + 1) - i * (2 * r + 2)
                                       : y + h + (r + 1) + i * (2 * r + 2);
        g.fillCircle(x + w / 2, dy, r, color);
    }
}

void drawBigNote(LovyanGFX &g, const Player &player, const PlaybackFrame &f)
{
    if (f.index < 0) return;  // 没在播：不画

    const vizmodel::Palette &p = palette();
    const jianpu::Score &s = player.score();

    // 拍点脉冲用亮度呼吸表达（拍首最亮、拍内衰减）。不用字号缩放：
    // 位图字号只能整数倍，缩放会跳。亮度只由 beatPhase 驱动，不画小节拍点圆
    const int level = vizmodel::beatLevel(vizmodel::beatPhase(f.elapsedMs, s.header.bpm));

    // 两侧淡色显示前一个 / 后一个音符
    drawGlyph(g, vizmodel::noteGlyphAt(s, f.index - 1), 40, kFxY + 40, 2, p.faint);
    drawGlyph(g, vizmodel::noteGlyphAt(s, f.index + 1), 200, kFxY + 40, 2, p.faint);

    // 正中超大显示当前音
    drawGlyph(g, vizmodel::noteGlyphAt(s, f.index), 120, kFxY + 15, 5,
              vizmodel::colorAt(p, level));
}
```

- [ ] **Step 3: 接进 `draw()` 的 switch**

在 `case Style::Wave:` 那一支之后追加：

```cpp
        case Style::BigNote:
            drawBigNote(g, player, f);
            break;
```

- [ ] **Step 4: 编译**

Run: `pio run -e cardputer-adv`
Expected: SUCCESS

- [ ] **Step 5: 烧写并上机目检**

Run: `make flash`

1. `.` 切到大字简谱：效果区正中一个超大数字，两侧淡色显示前一个 / 后一个音。
2. 大字随拍点呼吸（拍首最亮、拍内变暗），**字号始终不变**。
3. 高音音符上方有圆点、低音下方有圆点，最多两个；圆点没有压到顶栏或进度条上。
4. 休止符显示 `0` 且不画八度点。
5. 第一个音时左侧不画东西、最后一个音时右侧不画东西。
6. `SPC` 暂停：数字和亮度都冻住；恢复后继续呼吸。
7. 从大字简谱 `.` 一下回到频谱柱（四种风格闭环）。
8. 切回其它风格后字号正常（验证 `setTextSize(1)` 还原没漏）—— 特别是切回频谱柱再切回顶栏文字，顶栏字号不能变大。

- [ ] **Step 6: 提交**

```bash
git add src/viz_app.cpp
git commit -m "可视化：大字简谱风格（亮度呼吸的拍点脉冲）"
```

---

### Task 13: 文档与整体验收

**Files:**
- Modify: `README.md`（按键章节、代码结构清单、实测约束）

**Interfaces:**
- Consumes: 前 12 个任务的全部产出
- Produces: 无代码接口

- [ ] **Step 1: README 加可视化页键位表**

在 `### 曲库页` 那张表之后、`### 编辑页` 之前插入：

```markdown
### 可视化页

曲库页按 `空格` 播放成功就自动进入。读盘 / 解析失败、空谱、零时长时留在曲库页。

| 键 | 作用 |
|---|---|
| `空格` | 暂停 / 恢复 |
| `.` | 循环切换四种风格（频谱柱 / 音高卷帘 / 示波器 / 大字简谱） |
| `,` | 循环切换四种配色（灰 / 青 / 橙黄 / 绿） |
| `` ` `` | 停止并回曲库页 |

风格和配色只记在 RAM 里：本次开机内沿用，重启回到默认。曲子自然放完会自动回曲库页。

四种风格全部由**谱面数据**驱动，不采音频、不做 FFT —— 播放器是我们自己合成的音，
每个音的频率、起止时间、时值和 BPM 拍点都能确定性地算出来，比伪 FFT 更准也更便宜。
```

同时把 `### 曲库页` 表里 `空格` 那一行的说明改成：

```markdown
| `空格` | 直接播放，进全屏可视化页（不进编辑页） |
```

- [ ] **Step 2: README 更新代码结构清单**

在代码结构的 code block 里，`lib/remotemap/` 那行之后加一行，并在 `src/player.cpp` 之后加一行：

```
lib/vizmodel/      全屏可视化的纯逻辑：调色板、频谱柱、卷帘、波形、拍点、播放时钟
```
```
src/viz_app.cpp    全屏可视化页（四种风格 x 四种配色）
```

`test/` 那一行的用例数改成实际数字 —— 先跑 `pio test -e native`，把各 suite 的 Tests 数加起来，按跑出来的总数写。

- [ ] **Step 3: README 追加三条实测约束**

在「实测得出的约束」列表末尾追加：

```markdown
- **示波器的相位不能写成 `2π·f·t`**。换音时 `f` 跳变会让相位整体跳一大截，和「换音不跳变起点、只变密度」自相矛盾。固定角速度（`wavePhase` 只吃 `elapsed`）是唯一能同时满足「连续滚动」和「纯函数、可冻结」的写法。
- **每帧调用的函数不能返回 `std::vector`**。卷帘每秒要算 30 次方块表，在无 PSRAM 的 ESP32-S3 上持续搅动堆。所以 `rollBlocks` 由调用方给固定容量数组、函数只填不分配，越界的方块直接不填。
- **`Player::start()` 和 `stop()` 必须无条件清 `_paused`**。漏了的话「暂停 → `` ` `` 停止 → 立刻播另一首」会带着残留的 `_paused` 进新播放，`update()` 一进来就 return：喇叭全哑、画面冻在第一帧，而 `isPlaying()` 还是 true，看起来像死机。
- **可视化只用一定拿得到的 `bpm` 做拍点，不做小节线 / 小节内拍号**。`jianpu::Header` 只存 `keyRoot` 和 `bpm`，头部行里的拍号在解析时被显式丢弃，「每小节几拍」没有数据来源；而且不能简单拿分子当拍数（本项目的「拍」是 `60000/bpm` 毫秒的四分音符，6/8 一小节是 3 拍不是 6 拍）。
```

- [ ] **Step 4: 跑全部单测 + 编译**

Run: `pio test -e native`
Expected: 全部 suite PASSED（其中 test_vizmodel 38 个用例）

Run: `pio run -e cardputer-adv`
Expected: SUCCESS

- [ ] **Step 5: 上机整体验收（设计文档第 7 节的目检清单）**

Run: `make flash`

逐条走一遍，任何一条不过就回到对应任务修：
1. **四风格 × 四配色**：`.` 四下走完一圈回到起点；每种风格下 `,` 四下走完一圈；16 种组合都不崩、不黑屏、不刺眼。
2. **暂停 / 恢复补音**：在一个长音中途按 `SPC` 再恢复，听不出丢拍；在音符之间的静音间隙里暂停再恢复，不会多响一个音。
3. **暂停中按 `` ` `` 停止后立刻重播另一首**：新曲子正常出声、画面正常走（`_paused` 没残留）。
4. **自然放完自动退出**：回到曲库页且列表刷新过。
5. **30fps 下无闪烁**（离屏画布）。
6. **边界谱**：
   - 全是休止符的谱（如 `0 0 0 0`）：四种风格都有画面，不崩不黑屏。
   - 单音符 / 极短曲：进度条走完并自动退出。
   - 音域只有一个半音（如 `1 1 1`）：卷帘画在中线。
   - BPM 取 20 和 300（设置页改）：拍点呼吸正常，不卡不溢出。
7. **不该变的没变**：编辑页 `ENTER` 播放还是原样（原地高亮，不进可视化）；菜单页 / 背单词 / 遥控器三页行为不变；编辑页改动后 1.5 秒自动存盘仍正常。

- [ ] **Step 6: 提交**

```bash
git add README.md
git commit -m "README：可视化页键位、代码结构与新增的实测约束"
```

---

## 自查记录

**1. 设计覆盖**

| 设计要求 | 落在 |
|---|---|
| 进入 / 退出（`playById` 返回 true 才进；失败留在曲库页） | Task 9 改动 5、7 |
| `` ` `` 停止回列表 / 自然放完自动回列表 | Task 9 改动 7、8 |
| 页内键位 `SPC` `` ` `` `.` `,`，其余忽略 | Task 9 `viz_app::handleKey` |
| 风格 / 配色存 RAM，下次播放沿用 | Task 9 `gStyle` / `gPaletteIdx`（文件内静态） |
| 顶栏（曲号 + 预览截断 / 时间 / `II`）、效果区、进度条 | Task 1 `formatMmSs` / `progressWidth` + Task 9 `drawTopBar` / `drawProgress` |
| 风格 1 频谱柱（峰 + 邻柱衰减 + 包络 + 底噪） | Task 3 + Task 9 `drawSpectrum` |
| 风格 2 音高卷帘（时间窗口、竖线、三态着色、音域归一） | Task 4 + Task 10 |
| 风格 3 示波器（cycles / A / phase 各管一件事） | Task 5 + Task 11 |
| 风格 4 大字简谱（数字 + 八度点、亮度呼吸、前后音） | Task 6 + Task 12 |
| 休止符 = 无激励（四种风格各自的表现） | Task 3 底噪测试、Task 4 跳过休止符测试、Task 11 平线目检、Task 6 `0` 字形测试 |
| 四套调色板 | Task 1 |
| Player 三态机 + 转移表 + 不变量 | Task 8 |
| 恢复补音（> 0 且 freq > 0 才补） | Task 7 算术 + Task 8 `resume()` |
| `PlaybackFrame` / `score()` / `timeline()` | Task 8 |
| `viz_app` 不收 Score、标题拷进固定缓冲、`begin()` 算一次音域 | Task 9 `viz_app.h` 注释 + `begin()` |
| 卷帘固定容量缓冲（不每帧分配） | Task 4 cap 测试 + Task 10 `gBlocks[64]` |
| main：`Page::Viz`、30fps、自动退出、自动存盘守卫不动 | Task 9 |
| 错误与边界（读盘 / 解析 / 空谱 / 零时长 / 全休止 / 单半音 / 极端 BPM） | Task 9 改动 5、Task 2/4/6 的退化测试、Task 13 Step 5 第 6 条 |
| 测试策略（vizmodel 全覆盖、播放时钟必测、Player 走查、上机目检） | Task 1-7 单测、Task 8 Step 3、Task 13 Step 5 |
| 键位冲突检查（Viz 独占按键） | Task 9 改动 7（`handleKeys()` 按页分发） |

**2. 占位符扫描**：无 TBD / TODO / "类似 Task N" / "加上适当的错误处理"；每个代码步骤都给了可直接粘贴的完整代码块。

**3. 类型一致性**：`SpanSemi` / `RollGeom` / `RollBlock` / `RollState` / `NoteGlyph` / `PlaybackFrame` 的字段名与各 Task 的使用处逐一核对过；`kBarCount` / `kBarLowSemi` / `kBarSemiSpan` 被频谱柱（Task 3）和示波器（Task 5）共用，常量名一致；`rollBlockY` / `rollNowX` / `rollBlocks` 在 Task 4 定义、Task 10 使用，签名一致；`sinceOnset` 在 Task 9 定义、Task 11 复用。
