#include "lowbat_alert.h"

#include <M5Cardputer.h>
#include <lowbat.h>

#include <cstdio>

#include "timbre.h"

namespace {

constexpr uint32_t kPollMs = 5000;
constexpr uint32_t kNoteMs = 150;

// 下行的两个音，听起来像「没劲了」；紧急再来一遍
constexpr float kLowNotes[] = {880.0f, 659.3f};
constexpr float kCriticalNotes[] = {880.0f, 659.3f, 880.0f, 659.3f};

lowbat::Monitor gMon;
uint32_t gPollAt = 0;
bool gPolled = false;
bool gShowing = false;

const float *gNotes = nullptr;
int gNoteCount = 0;
int gNote = -1;  // 正在响第几个音，-1 = 不响
uint32_t gNoteAt = 0;

void playNote(int i)
{
    // NOISE 页开着麦克风时不响：麦克风和喇叭共用时钟脚，tone() 会顺手把喇叭
    // 拉起来、重装 I2S，把正在录的音掐断。这时只弹红框
    if (M5.Mic.isRunning()) return;
    // 默认波形在这块喇叭上几乎不出声，跟 AGENTS 的提示音一样用当前音色
    const timbre::Voice &v = timbre::current();
    M5Cardputer.Speaker.tone(gNotes[i], kNoteMs, -1, true, v.wave, v.len);
}

void startChime(const float *notes, int count)
{
    gNotes = notes;
    gNoteCount = count;
    gNote = 0;
    gNoteAt = millis();
    playNote(0);
}

}  // namespace

namespace lowbat_alert {

bool tick()
{
    const uint32_t now = millis();

    // 提示音不阻塞 loop：到点再换下一个音
    if (gNote >= 0 && now - gNoteAt >= kNoteMs) {
        if (++gNote < gNoteCount) {
            gNoteAt = now;
            playNote(gNote);
        } else {
            gNote = -1;
        }
    }

    if (gPolled && now - gPollAt < kPollMs) return false;
    gPolled = true;
    gPollAt = now;

    switch (gMon.feed(M5.Power.getBatteryVoltage())) {
        case lowbat::Event::BecameLow:
            startChime(kLowNotes, 2);
            break;
        case lowbat::Event::BecameCritical:
            startChime(kCriticalNotes, 4);
            break;
        case lowbat::Event::None:
            return false;
    }
    gShowing = true;
    return true;
}

bool iconRed()
{
    return lowbat::iconRed(gMon.level(), millis());
}

bool dismiss()
{
    if (!gShowing) return false;
    gShowing = false;
    return true;
}

void drawOverlay(LovyanGFX &g)
{
    if (!gShowing) return;

    constexpr int kX = 12, kY = 30, kW = 216, kH = 84;
    g.fillRoundRect(kX, kY, kW, kH, 6, TFT_RED);
    g.drawRoundRect(kX, kY, kW, kH, 6, TFT_WHITE);

    const bool critical = gMon.level() == lowbat::Level::Critical;
    char head[48];
    std::snprintf(head, sizeof(head), "%s  %d.%02dV", critical ? "电量快耗尽" : "电量低",
                  gMon.avgMv() / 1000, (gMon.avgMv() % 1000) / 10);

    g.setFont(&fonts::efontCN_14);
    g.setTextDatum(top_center);
    g.setTextColor(TFT_WHITE, TFT_RED);
    g.drawString(head, kX + kW / 2, kY + 10);
    g.drawString("请充电，侧面开关拨 ON", kX + kW / 2, kY + 34);
    g.setTextColor(0xFE18, TFT_RED);  // 浅粉，比正文弱一级
    g.drawString("任意键关闭", kX + kW / 2, kY + 58);
    g.setTextDatum(top_left);
}

}  // namespace lowbat_alert
