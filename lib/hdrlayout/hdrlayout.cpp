#include "hdrlayout.h"

namespace hdrlayout {

Fit fit(const std::string &title, const std::string &info, int cols)
{
    Fit out;
    const int infoLen = static_cast<int>(info.size());
    if (infoLen > cols) {
        out.info = cols >= 1 ? info.substr(0, cols - 1) + "~" : std::string();
        return out;
    }
    out.info = info;
    const int n = cols - infoLen - (info.empty() ? 0 : 1);
    if (static_cast<int>(title.size()) <= n) {
        out.title = title;
    } else if (n >= 2) {
        out.title = title.substr(0, n - 1) + "~";
    }
    return out;
}

std::string batteryDigits(int pct)
{
    if (pct < 0) return "--";
    if (pct > 99) pct = 99;
    std::string s;
    s += pct >= 10 ? static_cast<char>('0' + pct / 10) : ' ';
    s += static_cast<char>('0' + pct % 10);
    return s;
}

int batteryFill(int pct, int innerW)
{
    if (pct <= 0 || innerW <= 0) return 0;
    if (pct >= 100) return innerW;
    return pct * innerW / 100;
}

BtIcon btIcon(bool started, bool advertising, bool connected, uint32_t nowMs)
{
    if (!started) return BtIcon::Off;
    if (advertising) return (nowMs % 1000u) < 500u ? BtIcon::Bright : BtIcon::Dim;
    return connected ? BtIcon::Bright : BtIcon::Dim;
}

}  // namespace hdrlayout
