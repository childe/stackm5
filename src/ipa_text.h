// 画 IPA 音标。
//
// efontJA_16 覆盖了英语音标需要的 44/47 个符号，但缺三个：
//   ɪ U+026A  (bit 的元音)   ← 手写字形补上
//   ɛ U+025B  (bed 的元音)   ← 手写字形补上
//   ɝ U+025D  (bird 的元音)  ← 不补，词表里改写成 ɜr（ɜ 和 r 都有）
//
// 这三个在 efont 的 JA/TW/KR/CN 四个变体里都没有（逐个验证过：它们的位图
// 指纹完全相同，都是同一个「缺字」方块）。
//
// 音标里的字形全是半宽 8px，所以逐码位以 8px 推进就能排版，不需要测宽。
#pragma once

#include <M5GFX.h>

namespace ipa_text {

constexpr int kGlyphW = 8;
constexpr int kGlyphH = 16;

// 画一串 UTF-8 音标，返回占用的像素宽度
int draw(LovyanGFX &g, const char *utf8, int x, int y, uint16_t color);

// 一个码位在这里有手写字形吗（给测试和探针用）
bool hasCustomGlyph(uint32_t codepoint);

// 手写字形的位图：16 行，每行一个字节，bit7 = 最左像素。
// 返回 nullptr 表示这个码位没有手写字形。
const uint8_t *customGlyph(uint32_t codepoint);

}  // namespace ipa_text
