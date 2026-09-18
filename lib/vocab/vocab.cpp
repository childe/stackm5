#include "vocab.h"

#include <cstring>

namespace {

// 去掉两侧空白，这样源码里可以对齐 | 让词表好看
std::string trim(const std::string &s)
{
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;

    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;

    return s.substr(a, b - a);
}

}  // namespace

namespace vocab {

WordList parse(const char *text)
{
    WordList l;
    if (!text) return l;

    const char *p = text;
    size_t lineNo = 0;  // 1-based，且把注释和空行也算进去 —— 报错行号要能对上源文件

    while (*p) {
        ++lineNo;

        const char *eol = std::strchr(p, '\n');
        const size_t len = eol ? static_cast<size_t>(eol - p) : std::strlen(p);
        const std::string line = trim(std::string(p, len));

        const bool last = (eol == nullptr);
        p = last ? p + len : eol + 1;

        // 空行和 # 注释直接跳过
        if (line.empty() || line[0] == '#') {
            if (last) break;
            continue;
        }

        // 按前三个 | 切成四段：word | phonetic | definition | example
        // 只取前三个，所以例句（第四段）里可以含 |
        const size_t a = line.find('|');
        const size_t b = (a == std::string::npos) ? a : line.find('|', a + 1);
        const size_t c = (b == std::string::npos) ? b : line.find('|', b + 1);

        if (a == std::string::npos || b == std::string::npos) {
            l.error = {false, lineNo, "need word | phonetic | definition"};
            return l;
        }

        Word w;
        w.word = trim(line.substr(0, a));
        w.phonetic = trim(line.substr(a + 1, b - a - 1));
        if (c == std::string::npos) {
            w.definition = trim(line.substr(b + 1));  // 三段，没有例句
        } else {
            w.definition = trim(line.substr(b + 1, c - b - 1));
            w.example = trim(line.substr(c + 1));
        }

        if (w.word.empty()) {
            l.error = {false, lineNo, "empty word"};
            return l;
        }
        if (w.definition.empty()) {
            l.error = {false, lineNo, "empty definition"};
            return l;
        }

        l.words.push_back(w);

        if (last) break;
    }

    return l;
}


/*
 * 音标白名单。只包含在设备上逐个验证过有字形的符号：
 *   - efontJA_16 自带的 44 个（位图指纹互不相同 = 真字形）
 *   - 手写补上的 ɪ ɛ 两个（src/ipa_text.cpp）
 * ɝ 不在表里 —— 四个 efont 变体都没有，词表里改写成 ɜr。
 */
const char *kPhoneticWhitelist =
    // 结构符号
    "/'-. \u02c8\u02cc\u02d0"
    // 元音
    "iueoa"
    "\u026a"   // ɪ  手写
    "\u025b"   // ɛ  手写
    "\u00e6"   // æ
    "\u0251"   // ɑ
    "\u0252"   // ɒ
    "\u0254"   // ɔ
    "\u028a"   // ʊ
    "\u028c"   // ʌ
    "\u0259"   // ə
    "\u025c"   // ɜ
    "\u025a"   // ɚ
    // 辅音
    "pbtdkgfvszhmnlrjw"
    "\u03b8"   // θ
    "\u00f0"   // ð
    "\u0283"   // ʃ
    "\u0292"   // ʒ
    "\u014b"   // ŋ
    "\u0279"   // ɹ
    "\u027e";  // ɾ

std::vector<uint32_t> toCodepoints(const char *utf8)
{
    std::vector<uint32_t> out;
    if (!utf8) return out;

    const unsigned char *p = reinterpret_cast<const unsigned char *>(utf8);
    while (*p) {
        const unsigned char c = *p;
        if (c < 0x80) {
            out.push_back(c);
            p += 1;
        } else if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
            out.push_back(((c & 0x1Fu) << 6) | (p[1] & 0x3Fu));
            p += 2;
        } else if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
            out.push_back(((c & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu));
            p += 3;
        } else if ((c & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 &&
                   (p[3] & 0xC0) == 0x80) {
            out.push_back(((c & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) | ((p[2] & 0x3Fu) << 6) |
                          (p[3] & 0x3Fu));
            p += 4;
        } else {
            out.push_back(0xFFFD);  // 非法字节
            p += 1;
        }
    }
    return out;
}

}  // namespace vocab
