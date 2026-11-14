#include "textfit.h"

#include <cstdint>

std::string textfit::fit(LovyanGFX &g, const std::string &s, int maxW)
{
    if (g.textWidth(s.c_str()) <= maxW) return s;
    const int ellW = g.textWidth("…");
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        size_t len = 1;
        const unsigned char c = s[i];
        if (c >= 0xF0) len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;
        const std::string next = out + s.substr(i, len);
        if (g.textWidth(next.c_str()) + ellW > maxW) break;
        out = next;
        i += len;
    }
    return out + "…";
}

bool textfit::isAscii(const std::string &s)
{
    for (const char ch : s) {
        const uint8_t b = static_cast<uint8_t>(ch);
        if (b < 0x20 || b >= 0x7F) return false;
    }
    return true;
}
