// 单词表解析器 —— 纯 C++，零硬件依赖，可在电脑上单元测试。
// 格式规范见 docs/superpowers/specs/2026-09-18-vocab-app-design.md
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace vocab {

struct Word {
    std::string word;
    std::string phonetic;    // IPA 音标，可为空
    std::string definition;
    std::string example;     // 可为空
};

struct ParseError {
    bool ok = true;
    size_t line = 0;  // 1-based；0 表示无错
    const char *reason = "";
};

struct WordList {
    std::vector<Word> words;
    ParseError error;
};

WordList parse(const char *text);

// 把 UTF-8 拆成码位。给白名单校验和字形计数用。
std::vector<uint32_t> toCodepoints(const char *utf8);

// 屏幕能显示的上限：240x135 的屏幕上 AsciiFont8x16 是 30 列，
// 释义和例句各占 2 行 → 各 60 字符。超了在单元测试里就报错。
constexpr size_t kMaxDefinitionChars = 60;
constexpr size_t kMaxExampleChars = 60;

/*
 * 音标允许用到的字符，UTF-8 白名单。
 *
 * 为什么要白名单，而不是「非 ASCII 就放过」：设备上的字形不是想当然都有的。
 * efontJA_16 覆盖英语音标所需的 44/47 个符号，缺 ɪ ɛ ɝ 三个（efont 的
 * JA/TW/KR/CN 四个变体都缺，逐个验证过 —— 它们的位图指纹完全相同，
 * 都是同一个缺字方块）。其中 ɪ ɛ 用手写字形补上了（见 src/ipa_text.cpp），
 * ɝ 没补，词表里一律改写成 ɜr。
 *
 * 白名单之外的符号会在设备上显示成方块，而这在 Mac 上完全看不出来。
 * 单元测试拿这张表对账，把问题挡在烧写之前。
 */
extern const char *kPhoneticWhitelist;

// 音标最长多少个「字形」（不是字节 —— UTF-8 一个符号占 1-3 字节）。
// 屏幕上音标是 8px 等宽，240/8 = 30。
constexpr size_t kMaxPhoneticGlyphs = 30;

}  // namespace vocab
