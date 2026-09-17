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

        const size_t a = line.find('|');
        if (a == std::string::npos) {
            l.error = {false, lineNo, "missing | before definition"};
            return l;
        }

        const size_t b = line.find('|', a + 1);

        Word w;
        w.word = trim(line.substr(0, a));
        if (b == std::string::npos) {
            w.definition = trim(line.substr(a + 1));  // 只有两段，没有例句
        } else {
            // 只取前两个 | 作分隔，所以例句里可以含 |
            w.definition = trim(line.substr(a + 1, b - a - 1));
            w.example = trim(line.substr(b + 1));
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

}  // namespace vocab
