#include "diag_app.h"

#include <M5Cardputer.h>

#include <esp_system.h>

#include <cstdio>

#include "header.h"

namespace {

const char *resetName(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_SW: return "SW";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        default: return "OTHER";
    }
}

constexpr int kCharW = 8;
constexpr int kCharH = 16;

// I2C 扫描只在进页面时做一次：扫 112 个地址要几十毫秒，每帧都扫会卡。
bool gScanned = false;
uint8_t gFound[16];
size_t gFoundCount = 0;

// 电压极值。开机以来的最小/最大 —— 插拔电源或大负载时电轨会动，
// 一个恒定不动的值本身就是线索（说明它被充电 IC 钉住了）。
int gMinMv = 99999;
int gMaxMv = 0;

void scanI2C()
{
    bool present[128] = {false};
    M5.In_I2C.scanID(present);

    gFoundCount = 0;
    // 0x08~0x77 是 I2C 的有效从机地址范围，两端是保留地址
    for (uint8_t addr = 0x08; addr <= 0x77 && gFoundCount < sizeof(gFound); ++addr) {
        if (present[addr]) gFound[gFoundCount++] = addr;
    }
}

// 已知会出现在这块板子上的芯片，标出来省得对着地址猜。
// 没列出的地址照样显示，未知才是这次要找的东西。
const char *knownChip(uint8_t addr)
{
    switch (addr) {
        case 0x18: return "ES8311 codec";
        case 0x38: return "touch?";
        case 0x51: return "RTC?";
        case 0x68: return "IMU(MPU/BMI)";
        case 0x69: return "IMU alt";
        case 0x6B: return "BQ charger?";
        case 0x55: return "fuel gauge?";
        case 0x34: return "AXP PMIC?";
        case 0x75: return "AW9523 expander?";
        default: return "";
    }
}

void line(LovyanGFX &g, const char *s, int row, uint16_t color)
{
    g.setTextColor(color, TFT_BLACK);
    g.drawString(s, 0, row * kCharH);
}

}  // namespace

void diag_app::begin()
{
    gScanned = false;
    gMinMv = 99999;
    gMaxMv = 0;
}

void diag_app::draw(LovyanGFX &g)
{
    if (!gScanned) {
        scanI2C();
        gScanned = true;
    }

    const int mv = M5.Power.getBatteryVoltage();
    if (mv > 0) {
        if (mv < gMinMv) gMinMv = mv;
        if (mv > gMaxMv) gMaxMv = mv;
    }

    char buf[40];
    header::draw(g, "DIAG");
    int row = 1;

    // 电压是这次真正要看的量。4250mV 以上不可能是电芯 —— 锂电满电 4200mV。
    std::snprintf(buf, sizeof(buf), "bat  %d mV  %d%%", mv,
                  static_cast<int>(M5.Power.getBatteryLevel()));
    line(g, buf, row++, mv > 4250 ? TFT_RED : TFT_GREEN);

    std::snprintf(buf, sizeof(buf), "span %d..%d mV", gMinMv == 99999 ? 0 : gMinMv, gMaxMv);
    line(g, buf, row++, TFT_DARKGREY);

    // 上次重启的原因。原来这行显示充电状态，实机一直是 unknown（这块板子
    // 读不到），就换成这个：低电量起不来时，看这里是不是 BROWNOUT（掉压复位）。
    const esp_reset_reason_t rr = esp_reset_reason();
    std::snprintf(buf, sizeof(buf), "rst  %s", resetName(rr));
    line(g, buf, row++, rr == ESP_RST_BROWNOUT ? TFT_RED : TFT_DARKGREY);

    std::snprintf(buf, sizeof(buf), "i2c  %u found", static_cast<unsigned>(gFoundCount));
    line(g, buf, row++, TFT_CYAN);

    for (size_t i = 0; i < gFoundCount && row < 7; ++i) {
        std::snprintf(buf, sizeof(buf), " %02X %s", gFound[i], knownChip(gFound[i]));
        line(g, buf, row++, TFT_CYAN);
    }

    g.setTextColor(TFT_DARKGREY, TFT_BLACK);
    g.drawString("`back", 0, 7 * kCharH);
}

bool diag_app::handleKey(char c)
{
    if (c == '`') return false;
    if (c == 'r') begin();  // 重扫，顺便清掉电压极值
    return true;
}
