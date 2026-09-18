#include "ipa_text.h"

#include <cstring>

namespace {

/*
 * 手写字形。8 宽 × 16 高，每行一个字节，bit7 = 最左像素。
 * x 高度带是第 6-13 行 —— 这是从 efontJA_16 自带的 ə 量出来的。手写字形必须
 * 落在同一条基线上，否则混排时会高低不齐（第一版画在 5-12 行，实机回读
 * ASCII art 时看出底部衬线比字体的 l/i 高了一行）。
 *
 * 验证方法：把字形渲染到离屏画布，逐像素回读并以 ASCII art 从串口打出来 ——
 * 手画的东西不能只靠「编译通过」，得真的看见形状。
 */

// ɪ (U+026A) 小型大写 I：上下各一条横衬线，中间一根竖线
const uint8_t kGlyphSmallCapI[16] = {
    0b00000000,  //  0
    0b00000000,  //  1
    0b00000000,  //  2
    0b00000000,  //  3
    0b00000000,  //  4
    0b00000000,  //  5
    0b01111100,  //  6  ▄▄▄▄▄   x 高度上沿
    0b00010000,  //  7    █
    0b00010000,  //  8    █
    0b00010000,  //  9    █
    0b00010000,  // 10    █
    0b00010000,  // 11    █
    0b00010000,  // 12    █
    0b01111100,  // 13  ▄▄▄▄▄   基线，和字体字形对齐
    0b00000000,  // 14
    0b00000000,  // 15
};

// ɛ (U+025B) 希腊小写 epsilon 形：上弧、中横、下弧，开口朝右
const uint8_t kGlyphEpsilon[16] = {
    0b00000000,  //  0
    0b00000000,  //  1
    0b00000000,  //  2
    0b00000000,  //  3
    0b00000000,  //  4
    0b00000000,  //  5
    0b00111100,  //  6   ▄▄▄▄    x 高度上沿
    0b01000010,  //  7  █    █
    0b01000000,  //  8  █
    0b01111100,  //  9  █▄▄▄▄▄   中横
    0b01000000,  // 10  █
    0b01000000,  // 11  █
    0b01000010,  // 12  █    █
    0b00111100,  // 13   ▄▄▄▄    基线，和字体字形对齐
    0b00000000,  // 14
    0b00000000,  // 15
};

struct CustomGlyph {
    uint32_t cp;
    const uint8_t *rows;
};

const CustomGlyph kCustom[] = {
    {0x026A, kGlyphSmallCapI},
    {0x025B, kGlyphEpsilon},
};
constexpr size_t kCustomCount = sizeof(kCustom) / sizeof(kCustom[0]);

// 解出一个 UTF-8 码位，把 p 推进到下一个码位。非法字节按单字节跳过。
uint32_t nextCodepoint(const char *&p)
{
    const unsigned char c = static_cast<unsigned char>(*p);

    if (c < 0x80) {
        ++p;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        const uint32_t cp = ((c & 0x1Fu) << 6) | (p[1] & 0x3Fu);
        p += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        const uint32_t cp = ((c & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
        p += 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 &&
        (p[3] & 0xC0) == 0x80) {
        const uint32_t cp = ((c & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) |
                            ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
        p += 4;
        return cp;
    }

    ++p;  // 非法字节
    return 0xFFFD;
}

void drawCustom(LovyanGFX &g, const uint8_t *rows, int x, int y, uint16_t color)
{
    for (int row = 0; row < ipa_text::kGlyphH; ++row) {
        const uint8_t bits = rows[row];
        if (bits == 0) continue;
        for (int col = 0; col < ipa_text::kGlyphW; ++col) {
            if (bits & (0x80u >> col)) g.drawPixel(x + col, y + row, color);
        }
    }
}

}  // namespace

const uint8_t *ipa_text::customGlyph(uint32_t codepoint)
{
    for (size_t i = 0; i < kCustomCount; ++i) {
        if (kCustom[i].cp == codepoint) return kCustom[i].rows;
    }
    return nullptr;
}

bool ipa_text::hasCustomGlyph(uint32_t codepoint)
{
    return customGlyph(codepoint) != nullptr;
}

int ipa_text::draw(LovyanGFX &g, const char *utf8, int x, int y, uint16_t color)
{
    if (!utf8 || !*utf8) return 0;

    const int startX = x;
    const char *p = utf8;

    g.setTextDatum(top_left);

    while (*p) {
        const char *glyphStart = p;
        const uint32_t cp = nextCodepoint(p);

        if (const uint8_t *rows = customGlyph(cp)) {
            drawCustom(g, rows, x, y, color);
        } else {
            // 把这一个码位单独取出来交给字体画
            char one[5] = {0};
            const size_t len = static_cast<size_t>(p - glyphStart);
            std::memcpy(one, glyphStart, len < 4 ? len : 4);

            g.setFont(&fonts::efontJA_16);
            g.setTextColor(color, TFT_BLACK);
            g.drawString(one, x, y);
        }
        x += kGlyphW;
    }

    return x - startX;
}
