// 纯逻辑的文本编辑缓冲 —— 光标算术和折行，零硬件依赖，可在电脑上测。
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace texted {

// 一行在原文里的范围（折行不复制字符串，只记范围，
// 这样 UI 能把光标下标换算成「第几行第几列」）
struct Line {
    size_t start = 0;
    size_t len = 0;
};

class Buffer {
public:
    void setText(const std::string &t);
    const std::string &text() const
    {
        return _text;
    }
    size_t cursor() const
    {
        return _cursor;
    }

    void insert(char c);  // 在光标处插入，光标右移
    void backspace();     // 删掉光标前面那个字符
    void moveLeft();
    void moveRight();
    void moveHome();
    void moveEnd();

private:
    std::string _text;
    size_t _cursor = 0;
};

// 按空格折行，每行不超过 cols 个字符。
// 简谱的音符之间本来就用空格分隔，所以「在空格处折」等于「不拆开任何音符」。
std::vector<Line> wrapLines(const std::string &text, size_t cols);

// 光标落在第几行、该行第几列
void cursorRowCol(const std::vector<Line> &lines, size_t cursor, size_t &row, size_t &col);

}  // namespace texted
