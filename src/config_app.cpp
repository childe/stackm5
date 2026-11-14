#include "config_app.h"

#include <M5Cardputer.h>
#include <btdev.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ble.h"
#include "header.h"
#include "volume.h"

namespace {

// 屏幕旋转后 240x135。布局：
//   y 0..15     标题行（header 模块）
//   y 18..113   行区，每行 16px，可视 6 行，光标超出时滚动
//   y 119..134  按键提示
constexpr int kScreenW = 240;
constexpr int kCharW = 8;
constexpr int kRowH = 16;
constexpr int kRowsY = 18;
constexpr int kVisibleRows = 6;
constexpr int kHintY = 119;

constexpr int kRoleX = 16;
constexpr int kLabelX = 48;
constexpr size_t kLabelCols = 21;
constexpr int kDotX = 230;

// 行 key：音量行和 pair 行用两个不会是合法地址的值，和设备 key 放在同一个序列里，
// 这样重建列表时 relocateCursor 能统一找回光标
constexpr btdev::Key kVolumeRow = UINT64_MAX - 1;
constexpr btdev::Key kPairRow = UINT64_MAX;

std::vector<btdev::Entry> gDevs;
std::vector<btdev::Key> gRows;
int gCursor = 0;
int gTop = 0;
uint32_t gRev = 0;
int gShownSec = -1;
bool gConfirmForget = false;
bool gBondsFull = false;  // 「forget a device first」，直到下一次按键

void scrollToCursor()
{
    if (gCursor < gTop) gTop = gCursor;
    if (gCursor >= gTop + kVisibleRows) gTop = gCursor - kVisibleRows + 1;
    if (gTop < 0) gTop = 0;
}

void rebuild()
{
    gRev = ble::devicesRevision();
    gDevs = ble::devices();

    std::vector<btdev::Key> rows;
    rows.reserve(gDevs.size() + 2);
    rows.push_back(kVolumeRow);
    for (const auto &d : gDevs) rows.push_back(d.key);
    rows.push_back(kPairRow);

    gCursor = gRows.empty() ? 0 : btdev::relocateCursor(gRows, gCursor, rows);
    gRows = rows;
    scrollToCursor();
}

btdev::Key rowKey()
{
    if (gCursor < 0 || gCursor >= static_cast<int>(gRows.size())) return kVolumeRow;
    return gRows[gCursor];
}

const btdev::Entry *rowDevice()
{
    const btdev::Key k = rowKey();
    if (k == kVolumeRow || k == kPairRow) return nullptr;
    for (const auto &d : gDevs) {
        if (d.key == k) return &d;
    }
    return nullptr;
}

// 位图字体只有 ASCII：非 ASCII 的名字（中文电视名）每个多字节字符换成一个 '?'，
// 再截到 cols 列，保证不越界、不拖垮布局
std::string printable(const std::string &s, size_t cols)
{
    std::string out;
    for (size_t i = 0; i < s.size() && out.size() < cols; ++i) {
        const uint8_t b = static_cast<uint8_t>(s[i]);
        if (b >= 0x20 && b < 0x7F) {
            out += static_cast<char>(b);
        } else if ((b & 0xC0) != 0x80) {
            out += '?';  // 控制字符或多字节字符的前导字节；续字节跳过
        }
    }
    return out;
}

const char *roleText(btdev::Role r)
{
    switch (r) {
        case btdev::Role::Tv:
            return "TV";
        case btdev::Role::Mac:
            return "Mac";
        default:
            return "";
    }
}

}  // namespace

namespace config_app {

void begin()
{
    ble::begin();
    gRows.clear();
    gCursor = 0;
    gTop = 0;
    gConfirmForget = false;
    gBondsFull = false;
    gShownSec = -1;
    rebuild();
}

void end()
{
    ble::stopPairing();
    gConfirmForget = false;
    gBondsFull = false;
}

bool tick()
{
    bool dirty = false;
    if (ble::devicesRevision() != gRev) {
        rebuild();
        dirty = true;
    }
    const int sec = ble::pairing() ? ble::pairingRemainingSec() : -1;
    if (sec != gShownSec) {
        gShownSec = sec;
        dirty = true;
    }
    return dirty;
}

void draw(LovyanGFX &g)
{
    header::draw(g, "CONFIG");

    const bool pairing = ble::pairing();
    char buf[48];
    for (int r = 0; r < kVisibleRows; ++r) {
        const int i = gTop + r;
        if (i >= static_cast<int>(gRows.size())) break;
        const int y = kRowsY + r * kRowH;
        const bool sel = (i == gCursor);
        const uint16_t fg = sel ? TFT_CYAN : TFT_LIGHTGREY;

        if (sel) {
            g.setTextColor(TFT_CYAN, TFT_BLACK);
            g.drawString(">", 0, y);
        }

        const btdev::Key k = gRows[i];
        if (k == kVolumeRow) {
            g.setTextColor(fg, TFT_BLACK);
            g.drawString("volume", kRoleX, y);
            std::snprintf(buf, sizeof(buf), "- %d +", volume::level());
            g.drawString(buf, kScreenW - static_cast<int>(std::strlen(buf)) * kCharW - kCharW, y);
        } else if (k == kPairRow) {
            if (pairing) {
                std::snprintf(buf, sizeof(buf), "pairing... %ds", ble::pairingRemainingSec());
                g.setTextColor(TFT_GREEN, TFT_BLACK);
            } else {
                std::snprintf(buf, sizeof(buf), "+ pair new device");
                g.setTextColor(fg, TFT_BLACK);
            }
            g.drawString(buf, kRoleX, y);
        } else {
            const btdev::Entry *d = nullptr;
            for (const auto &e : gDevs) {
                if (e.key == k) {
                    d = &e;
                    break;
                }
            }
            if (!d) continue;
            g.setTextColor(sel ? TFT_CYAN : TFT_DARKGREY, TFT_BLACK);
            g.drawString(roleText(d->role), kRoleX, y);
            g.setTextColor(fg, TFT_BLACK);
            g.drawString(printable(d->label, kLabelCols).c_str(), kLabelX, y);
            if (d->connected) g.fillCircle(kDotX, y + 7, 3, TFT_GREEN);
        }
    }

    // 底行提示（≤ 30 列）
    const char *hint;
    uint16_t hintColor = TFT_DARKGREY;
    if (gBondsFull) {
        hint = "forget a device first";
        hintColor = TFT_RED;
    } else if (gConfirmForget) {
        hint = "DEL again to forget";
        hintColor = TFT_RED;
    } else if (pairing) {
        hint = ";. ENT cancel `back";
    } else if (rowKey() == kVolumeRow) {
        hint = ";. move =- vol `back";
    } else if (rowKey() == kPairRow) {
        hint = ";. =- vol ENT pair `back";
    } else {
        hint = ";. =- vol DEL forget `back";
    }
    g.setTextColor(hintColor, TFT_BLACK);
    g.drawString(hint, 0, kHintY);
}

bool handleChar(char c)
{
    gBondsFull = false;
    if (c == '`') return false;

    if (c == '\b') {
        const btdev::Entry *d = rowDevice();
        if (!d) {
            gConfirmForget = false;
            return true;
        }
        if (gConfirmForget) {
            gConfirmForget = false;
            ble::forget(d->key);  // revision 变化后 tick() 重建列表，光标停在同序号
        } else {
            gConfirmForget = true;
        }
        return true;
    }

    gConfirmForget = false;
    const int last = static_cast<int>(gRows.size()) - 1;
    if (c == ';') {
        if (gCursor > 0) --gCursor;
        scrollToCursor();
    } else if (c == '.') {
        if (gCursor < last) ++gCursor;
        scrollToCursor();
    } else if (c == '=') {
        volume::up();
    } else if (c == '-') {
        volume::down();
    }
    return true;
}

void handleEnter()
{
    gConfirmForget = false;
    gBondsFull = false;
    if (ble::pairing()) {
        ble::stopPairing();
        return;
    }
    if (rowKey() != kPairRow) return;
    if (ble::startPairing() == ble::PairStart::BondsFull) gBondsFull = true;
}

}  // namespace config_app
