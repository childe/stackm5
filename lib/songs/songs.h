// 内置示例谱子。纯数据，零硬件依赖 —— 这样单元测试能在电脑上验证
// 每一首都解析得通、拍数对得上（手写的谱面字符串最容易漏空格、错符号）。
//
// title 只给测试和文档用；设备上的曲库靠「编号 + 谱面预览」认歌，不存歌名。
#pragma once

#include <cstddef>
#include <string>

namespace songs {

struct Builtin {
    const char *title;
    const char *header;  // 形如 "1=C 4/4 120"
    const char *notes;
    size_t noteCount;  // 期望解析出多少个音符
    float beats;       // 期望总拍数
};

extern const Builtin kBuiltins[];
extern const size_t kBuiltinCount;

// 拼成存盘用的完整文本（头部行 + 换行 + 音符）
std::string fullTextOf(const Builtin &b);

}  // namespace songs
