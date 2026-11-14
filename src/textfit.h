// 按像素宽度截断 UTF-8 文本，放不下时末尾加「…」。用当前字体量宽，调用前先 setFont。
// AGENTS 的 session 标题和 CONFIG 的设备名共用
#pragma once

#include <M5GFX.h>

#include <string>

namespace textfit {

std::string fit(LovyanGFX &g, const std::string &s, int maxW);

// 全是可打印 ASCII（0x20..0x7E）→ true。CONFIG 据此决定用 8x16 位图字体还是中文字体
bool isAscii(const std::string &s);

}  // namespace textfit
