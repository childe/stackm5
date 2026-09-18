#include "vocab_app.h"

#include <esp_random.h>
#include <texted.h>
#include <vocab.h>
#include <wordlist.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// 屏幕 240x135，AsciiFont8x16 严格等宽 → 30 列
constexpr size_t kCols = 30;
constexpr int kCharW = 8;

// 像素预算刚好排满 135px：
//   标题 16 + 单词 32 + 释义 2x16 + 例句 2x16 + 提示 16 = 135
constexpr int kTitleY = 0;
constexpr int kWordY = 18;
constexpr int kDefY = 52;
constexpr int kExampleY = 86;
constexpr int kHintY = 119;
constexpr int kLineH = 16;
constexpr size_t kMaxLines = 2;  // 释义和例句各最多 2 行

// 单词用 textSize(2) → 16x32，超过这么多字符就降回小字号，不截断
constexpr size_t kBigWordMaxChars = 15;

// 全灰度调色板。彩色（黄/青）在这块 1.14" IPS 上太刺眼 —— 实机看出来的。
// RGB565 的灰阶：((v>>3)<<11) | ((v>>2)<<5) | (v>>3)
constexpr uint16_t kFgWord = 0xFFFF;  // 灰度 255，单词是焦点，最亮
constexpr uint16_t kFgBody = 0xD69A;  // 灰度 208，释义和例句 —— 同亮度同字号
constexpr uint16_t kFgDim = 0x9492;   // 灰度 144，标题
constexpr uint16_t kFgFaint = 0x738E; // 灰度 112，词数和按键提示
constexpr uint16_t kFgRule = 0x528A;  // 灰度  80，释义与例句之间的分隔线

vocab::WordList gList;
size_t gIndex = 0;
bool gRevealed = false;

void pickRandom()
{
    if (gList.words.empty()) return;

    // esp_random() 是硬件熵源，不需要播种。
    // 用 Arduino 的 random() 的话不 randomSeed() 每次开机顺序完全一样。
    gIndex = esp_random() % gList.words.size();
    gRevealed = false;
}

// 画一段可能要折行的文本，最多 kMaxLines 行
void drawWrapped(LovyanGFX &g, const std::string &text, int y, uint16_t color)
{
    if (text.empty()) return;

    g.setTextColor(color, TFT_BLACK);

    // 复用简谱 app 的折行 —— AsciiFont8x16 等宽，按字符数折就是对的
    const std::vector<texted::Line> lines = texted::wrapLines(text, kCols);
    for (size_t i = 0; i < lines.size() && i < kMaxLines; ++i) {
        const texted::Line &ln = lines[i];
        g.drawString(text.substr(ln.start, ln.len).c_str(), 0, y + static_cast<int>(i) * kLineH);
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
        std::snprintf(msg, sizeof(msg), "wordlist line %u:", static_cast<unsigned>(gList.error.line));
        g.setTextColor(kFgWord, TFT_BLACK);
        g.drawString(msg, 0, kTitleY);
        g.drawString(gList.error.reason, 0, kTitleY + kLineH);
        g.setTextColor(kFgFaint, TFT_BLACK);
        g.drawString("`back", 0, kHintY);
        return;
    }

    if (gList.words.empty()) {
        g.setTextColor(kFgFaint, TFT_BLACK);
        g.drawString("wordlist is empty", 0, kTitleY);
        g.drawString("`back", 0, kHintY);
        return;
    }

    const vocab::Word &w = gList.words[gIndex];

    // ── 标题 ────────────────────────────────────────────────
    g.setTextColor(kFgDim, TFT_BLACK);
    g.drawString("VOCAB", 0, kTitleY);

    char count[20];
    std::snprintf(count, sizeof(count), "%u words", static_cast<unsigned>(gList.words.size()));
    g.setTextColor(kFgFaint, TFT_BLACK);
    g.drawString(count, g.width() - static_cast<int>(std::strlen(count)) * kCharW, kTitleY);

    // ── 单词（长词自动降字号，不截断）──────────────────────
    g.setTextColor(kFgWord, TFT_BLACK);
    g.setTextSize(w.word.size() <= kBigWordMaxChars ? 2 : 1);
    g.drawString(w.word.c_str(), 0, kWordY);
    g.setTextSize(1);

    // ── 释义和例句（翻开后才显示）──────────────────────────
    if (gRevealed) {
        drawWrapped(g, w.definition, kDefY, kFgBody);
        drawWrapped(g, w.example, kExampleY, kFgBody);

        // 两块同色同字号了，靠一条 1px 细线区分释义和例句。
        // 画在 kDefY+2*kLineH 的缝里（释义占到 84，例句从 86 开始）。
        if (!w.example.empty()) {
            g.drawFastHLine(0, kDefY + static_cast<int>(kMaxLines) * kLineH, g.width() / 2,
                            kFgRule);
        }
    }

    g.setTextColor(kFgFaint, TFT_BLACK);
    g.drawString(gRevealed ? "SPACE next  `back" : "SPACE flip  ENTER skip  `back", 0, kHintY);
}

bool vocab_app::handleKey(char c)
{
    switch (c) {
        case '`':
            return false;  // 回菜单页

        case ' ':
            // 一个键两用：没翻开就翻开，翻开了就换下一个
            if (gRevealed) {
                pickRandom();
            } else {
                gRevealed = true;
            }
            break;

        case '\n':  // ⏎：跳过，不翻开直接换下一个
            pickRandom();
            break;

        default:
            break;
    }
    return true;
}
