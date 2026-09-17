// 单词表解析器 —— 纯 C++，零硬件依赖，可在电脑上单元测试。
// 格式规范见 docs/superpowers/specs/2026-09-18-vocab-app-design.md
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace vocab {

struct Word {
    std::string word;
    std::string definition;
    std::string example;  // 可为空
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

// 屏幕能显示的上限：240x135 的屏幕上 AsciiFont8x16 是 30 列，
// 释义和例句各占 2 行 → 各 60 字符。超了在单元测试里就报错。
constexpr size_t kMaxDefinitionChars = 60;
constexpr size_t kMaxExampleChars = 60;

}  // namespace vocab
