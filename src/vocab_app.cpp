#include "vocab_app.h"

#include <esp_random.h>
#include <texted.h>
#include <vocab.h>
#include <wordlist.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ipa_text.h"

namespace {

/*
 * 两步翻卡片：单词 → 释义 → 例句，SPACE 依次推进。
 *
 * 为什么不把释义和例句放一屏：字号和信息量直接对冲。实测这 100 条数据，
 * 同屏时只能用 AsciiFont8x16（30 列，释义+例句共 64px）；换成
 * FreeMono12pt（14x24，17 列）就要 144px，而屏幕只有 135px。
 * 分两步之后每块各自占满，才能上大字号。
 */

// 标题、单词、音标用的等宽位图字体
constexpr int kSmallW = 8;
constexpr int kSmallH = 16;

// 释义和例句用 FreeMono12pt：14x24 等宽 → 17 列，一块最多 3 行
constexpr int kBodyH = 24;
constexpr size_t kBodyCols = 17;
constexpr size_t kBodyLines = 3;

// 单词用 textSize(2) → 16x32；超过这么多字符降回小字号，不截断
constexpr size_t kBigWordMaxChars = 15;

// 全灰度。彩色在这块 1.14" IPS 上太刺眼（实机反馈）。
constexpr uint16_t kFgWord = 0xFFFF;   // 灰度 255，单词最亮
constexpr uint16_t kFgBody = 0xD69A;   // 灰度 208，释义和例句同亮度同字号
constexpr uint16_t kFgDim = 0x9492;    // 灰度 144，音标、次级标题
constexpr uint16_t kFgFaint = 0x738E;  // 灰度 112，词数和提示

// 卡片的三个状态
enum class Step { Word, Meaning, Example };

vocab::WordList gList;
size_t gIndex = 0;
Step gStep = Step::Word;

void pickRandom()
{
    if (gList.words.empty()) return;

    // esp_random() 是硬件熵源，不需要播种。
    // 用 Arduino 的 random() 的话不 randomSeed() 每次开机顺序完全一样。
    gIndex = esp_random() % gList.words.size();
    gStep = Step::Word;
}

// 画一段折行文本。cols/lineH 由调用方给 —— 两种字体的度量不同。
void drawWrapped(LovyanGFX &g, const std::string &text, int y, size_t cols, int lineH,
                 size_t maxLines, uint16_t color)
{
    if (text.empty()) return;

    g.setTextColor(color, TFT_BLACK);

    const std::vector<texted::Line> lines = texted::wrapLines(text, cols);
    for (size_t i = 0; i < lines.size() && i < maxLines; ++i) {
        const texted::Line &ln = lines[i];
        g.drawString(text.substr(ln.start, ln.len).c_str(), 0, y + static_cast<int>(i) * lineH);
    }
}

void drawSmall(LovyanGFX &g, const char *s, int x, int y, uint16_t color, uint8_t size = 1)
{
    g.setFont(&fonts::AsciiFont8x16);
    g.setTextSize(size);
    g.setTextColor(color, TFT_BLACK);
    g.drawString(s, x, y);
    g.setTextSize(1);
}

// ── 三个状态各自的画法 ──────────────────────────────────────

// 单词页：提示就放在释义将要出现的那块空白里 —— 翻开前那里本来是空的，
// 等于不花额外空间；翻开后自动让位给内容，所以底部不需要常驻提示行。
void drawWordStep(LovyanGFX &g, const vocab::Word &w)
{
    drawSmall(g, "VOCAB", 0, 0, kFgDim);

    char count[20];
    std::snprintf(count, sizeof(count), "%u words", static_cast<unsigned>(gList.words.size()));
    drawSmall(g, count, g.width() - static_cast<int>(std::strlen(count)) * kSmallW, 0, kFgFaint);

    drawSmall(g, w.word.c_str(), 0, 30, kFgWord, w.word.size() <= kBigWordMaxChars ? 2 : 1);

    drawSmall(g, "SPACE  meaning", 0, 76, kFgFaint);
    drawSmall(g, "ENTER  skip", 0, 94, kFgFaint);
    drawSmall(g, "`      back", 0, 112, kFgFaint);
}

// 释义页：单词 + 音标 + 大字号释义。
// 像素预算：单词 32 + 音标 16 + 释义 3x24 = 120，装得进 135。
void drawMeaningStep(LovyanGFX &g, const vocab::Word &w)
{
    drawSmall(g, w.word.c_str(), 0, 0, kFgWord, w.word.size() <= kBigWordMaxChars ? 2 : 1);

    if (!w.phonetic.empty()) {
        // 音标必须逐码位画：efontJA_16 缺 ɪ ɛ，用手写字形补（见 ipa_text.cpp）
        ipa_text::draw(g, w.phonetic.c_str(), 0, 36, kFgDim);
    }

    g.setFont(&fonts::FreeMono12pt7b);
    drawWrapped(g, w.definition, 58, kBodyCols, kBodyH, kBodyLines, kFgBody);
}

// 例句页：单词用小字号当参照，例句用大字号
void drawExampleStep(LovyanGFX &g, const vocab::Word &w)
{
    drawSmall(g, w.word.c_str(), 0, 0, kFgDim);

    g.setFont(&fonts::FreeMono12pt7b);
    if (w.example.empty()) {
        g.setTextColor(kFgFaint, TFT_BLACK);
        g.drawString("(no example)", 0, 24);
    } else {
        drawWrapped(g, w.example, 24, kBodyCols, kBodyH, kBodyLines, kFgBody);
    }
}

}  // namespace

void vocab_app::begin()
{
    if (gList.words.empty()) {
        gList = vocab::parse(vocab::kRawWords);
    }
    pickRandom();
}

void vocab_app::draw(LovyanGFX &g)
{
    // 词表解析失败：把行号显示出来，别让用户对着空屏幕猜
    if (!gList.error.ok) {
        char msg[64];
        std::snprintf(msg, sizeof(msg), "wordlist line %u:",
                      static_cast<unsigned>(gList.error.line));
        drawSmall(g, msg, 0, 0, kFgWord);
        drawSmall(g, gList.error.reason, 0, kSmallH, kFgBody);
        drawSmall(g, "`back", 0, 112, kFgFaint);
        return;
    }

    if (gList.words.empty()) {
        drawSmall(g, "wordlist is empty", 0, 0, kFgFaint);
        drawSmall(g, "`back", 0, 112, kFgFaint);
        return;
    }

    const vocab::Word &w = gList.words[gIndex];

    switch (gStep) {
        case Step::Word:
            drawWordStep(g, w);
            break;
        case Step::Meaning:
            drawMeaningStep(g, w);
            break;
        case Step::Example:
            drawExampleStep(g, w);
            break;
    }
}

bool vocab_app::handleKey(char c)
{
    switch (c) {
        case '`':
            return false;  // 回菜单页

        case ' ':
            // 一个键推进三步，走完换下一个词
            if (gStep == Step::Word) {
                gStep = Step::Meaning;
            } else if (gStep == Step::Meaning) {
                gStep = Step::Example;
            } else {
                pickRandom();
            }
            break;

        case '\n':  // ⏎：跳过，不看释义直接换下一个
            pickRandom();
            break;

        default:
            break;
    }
    return true;
}
