#include "texted.h"

namespace texted {

void Buffer::setText(const std::string &t)
{
    _text = t;
    _cursor = _text.size();  // 打开已有谱子时，光标停末尾接着往后敲
}

void Buffer::insert(char c)
{
    _text.insert(_cursor, 1, c);
    ++_cursor;
}

void Buffer::backspace()
{
    if (_cursor == 0) return;
    _text.erase(_cursor - 1, 1);
    --_cursor;
}

void Buffer::moveLeft()
{
    if (_cursor > 0) --_cursor;
}

void Buffer::moveRight()
{
    if (_cursor < _text.size()) ++_cursor;
}

void Buffer::moveHome()
{
    _cursor = 0;
}

void Buffer::moveEnd()
{
    _cursor = _text.size();
}

std::vector<Line> wrapLines(const std::string &text, size_t cols)
{
    std::vector<Line> lines;
    if (cols == 0) cols = 1;

    const size_t n = text.size();
    size_t pos = 0;

    while (pos < n) {
        // 剩下的能一行装完
        if (n - pos <= cols) {
            Line ln;
            ln.start = pos;
            ln.len = n - pos;
            lines.push_back(ln);
            pos = n;
            break;
        }

        // 贪心：在 (pos, pos+cols] 里找最靠后的空格当断点
        size_t breakAt = std::string::npos;
        for (size_t k = pos + cols; k > pos; --k) {
            if (text[k] == ' ') {
                breakAt = k;
                break;
            }
        }

        if (breakAt == std::string::npos) {
            // 一个 token 比一整行还长（比如 "1---------"），只能硬切，
            // 否则这里会死循环
            Line ln;
            ln.start = pos;
            ln.len = cols;
            lines.push_back(ln);
            pos += cols;
        } else {
            Line ln;
            ln.start = pos;
            ln.len = breakAt - pos;
            lines.push_back(ln);
            pos = breakAt + 1;  // 断点处的空格不显示
        }
    }

    // 空文本也要有一行，不然屏幕上画不出光标
    if (lines.empty()) lines.push_back(Line());

    return lines;
}

void cursorRowCol(const std::vector<Line> &lines, size_t cursor, size_t &row, size_t &col)
{
    row = 0;
    col = 0;
    if (lines.empty()) return;

    for (size_t i = 0; i < lines.size(); ++i) {
        const Line &ln = lines[i];
        const bool isLast = (i + 1 == lines.size());

        // 光标可以停在行尾（下标 == start + len），所以是 <=
        if (cursor <= ln.start + ln.len || isLast) {
            row = i;
            col = (cursor > ln.start) ? cursor - ln.start : 0;
            if (col > ln.len) col = ln.len;
            return;
        }
    }
}

}  // namespace texted
