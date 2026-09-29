#include "noise_app.h"

#include <M5Cardputer.h>
#include <Preferences.h>
#include <noisemeter.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// 屏幕旋转后 240x135。布局：
//   y 0..15    顶栏：左 NOISE，右 = 自清零以来的最大值 + 报警线（调校准时换成校准值）
//   y 18..66   大数字（每 250ms 一变）+ dB + 文字档位
//   y 72..83   实时音量条 + 峰值线 + 报警线刻度
//   y 88..109  最近一分钟曲线
//   y 112..127 按键提示
constexpr int kScreenW = 240;
constexpr int kCharW = 8;
constexpr int kTitleY = 0;
constexpr int kNumY = 18;
constexpr int kBarY = 72;
constexpr int kBarH = 12;
constexpr int kGraphY = 88;
constexpr int kGraphH = 22;
constexpr int kHintY = 112;

// 音量条和曲线的量程（显示用的 dB）：安静的房间在底部，大声喊接近顶部
constexpr float kLoDb = 30.0f;
constexpr float kHiDb = 100.0f;

constexpr uint32_t kSampleRate = 16000;
constexpr size_t kBlockLen = 256;
constexpr uint32_t kBlockMs = kBlockLen * 1000 / kSampleRate;  // 16ms

// 开麦克风后丢掉的块数（约 200ms）。实测 ES8311 上电后头 250ms 读数比环境高
// 十几 dB，是编解码器的启动瞬态；不丢的话它会被当成「最大值」一直挂在顶栏
constexpr uint32_t kWarmupBlocks = 12;

// 重绘间隔：20fps。音量条是实时的，但再快肉眼也分辨不出
constexpr uint32_t kFrameMs = 50;

// 校准值默认让安静的房间读 35 dB 左右（实机量出来的，见 README）
constexpr int kDefaultCal = 105;
constexpr int kDefaultAlarm = 70;

// 两块轮流录：一块录满，回调里立刻把它重新排上，另一块已经在排队，录音不断
int16_t gBuf[2][kBlockLen];

// 录音任务 → 主循环：单生产者单消费者环形队列，只传每块的均方值。
// 64 块 ≈ 1 秒，主循环卡得再久也就丢最老的
constexpr uint32_t kQueueLen = 64;
float gQueue[kQueueLen];
std::atomic<uint32_t> gHead{0};
uint32_t gTail = 0;
uint32_t gSkip = 0;  // 还要丢掉几块（启动瞬态）

bool gMicOk = false;
uint32_t gAudioMs = 0;  // 按块累加的音频时间
uint32_t gLastFrameMs = 0;
uint32_t gCalShownUntilMs = 0;

// 计量都用原始 dBFS，显示时再加校准值：这样调校准时历史曲线整体平移，不会断层
noisemeter::Level gLevel;
noisemeter::PeakHold gPeak;
noisemeter::History gHistory;
noisemeter::Alarm gAlarm;

int gCal = kDefaultCal;
int gAlarmDb = kDefaultAlarm;

uint16_t grey(int v)
{
    return LovyanGFX::color565(v, v, v);
}

// 报警时整屏闪的底色：暗红，不刺眼（这块屏上高亮色很扎眼）
const uint16_t kFlashBg = LovyanGFX::color565(110, 0, 0);

float shown(float rawDb)
{
    const float v = rawDb + static_cast<float>(gCal);
    return v < 0.0f ? 0.0f : v;
}

void loadSettings()
{
    // 用读写模式打开：第一次进这一页时命名空间还不存在，只读打开会在串口报 NOT_FOUND
    Preferences p;
    p.begin("noise", false);
    gCal = p.getInt("cal", kDefaultCal);
    gAlarmDb = p.getInt("alarm", kDefaultAlarm);
    p.end();
}

void saveSettings()
{
    // 只在按键调整时写，一次两个整数：NVS 自带磨损均衡，按键频率下不心疼
    Preferences p;
    p.begin("noise", false);
    p.putInt("cal", gCal);
    p.putInt("alarm", gAlarmDb);
    p.end();
}

// 录音任务里调：算这一块的能量塞进队列，再把这块缓冲重新排上。
// 这里不能阻塞，也不能调 begin()/end()（见 Mic_Class.hpp 的说明）
void onBlock(void *, void *data, size_t len)
{
    const float ms = noisemeter::meanSquare(static_cast<const int16_t *>(data), len);
    const uint32_t h = gHead.load(std::memory_order_relaxed);
    gQueue[h % kQueueLen] = ms;
    gHead.store(h + 1, std::memory_order_release);
    M5.Mic.record(static_cast<int16_t *>(data), len);
}

void resetMeters()
{
    gLevel.reset();
    gPeak.reset();
    gHistory.reset();
    gAlarm.reset();
}

void drawHistory(LovyanGFX &g)
{
    const int n = gHistory.size();
    const float alarmFrac = noisemeter::fraction(static_cast<float>(gAlarmDb), kLoDb, kHiDb);
    const int alarmY = kGraphY + kGraphH - 1 - static_cast<int>(alarmFrac * (kGraphH - 1));

    // 最新的在最右边
    for (int i = 0; i < n; ++i) {
        const float db = shown(gHistory.at(i));
        const int h = static_cast<int>(noisemeter::fraction(db, kLoDb, kHiDb) * kGraphH);
        if (h <= 0) continue;
        const int x = kScreenW - n + i;
        // 超过报警线的那几列画亮一点，回头看得出是哪一下超的
        g.drawFastVLine(x, kGraphY + kGraphH - h, h, db >= gAlarmDb ? TFT_WHITE : grey(140));
    }

    // 报警线：虚线
    for (int x = 0; x < kScreenW; x += 4) g.drawPixel(x, alarmY, grey(110));
}

}  // namespace

void noise_app::begin()
{
    loadSettings();
    resetMeters();
    gHead.store(0);
    gTail = 0;
    gSkip = kWarmupBlocks;
    gAudioMs = 0;
    gLastFrameMs = 0;
    gCalShownUntilMs = 0;

    // 麦克风和喇叭共用时钟脚，只能二选一
    M5Cardputer.Speaker.end();
    M5.Mic.setBufferReleaseCallback(nullptr, onBlock);
    gMicOk = M5.Mic.begin();
    if (gMicOk) {
        gMicOk = M5.Mic.record(gBuf[0], kBlockLen, kSampleRate) &&
                 M5.Mic.record(gBuf[1], kBlockLen, kSampleRate);
    }
}

void noise_app::end()
{
    M5.Mic.end();
    // 回调只能在 end() 返回之后清（录音任务那时才确定不会再调它）
    M5.Mic.setBufferReleaseCallback(nullptr, nullptr);
    // 音量档位存在 Speaker 对象里，end/begin 不会丢
    M5Cardputer.Speaker.begin();
}

bool noise_app::tick()
{
    const uint32_t head = gHead.load(std::memory_order_acquire);
    if (head - gTail > kQueueLen) gTail = head - kQueueLen;  // 落后太多：丢掉最老的

    while (gTail != head) {
        const float ms = gQueue[gTail % kQueueLen];
        ++gTail;
        if (gSkip > 0) {
            --gSkip;
            continue;
        }

        gAudioMs += kBlockMs;
        gLevel.push(ms, static_cast<float>(kBlockMs));
        const float db = gLevel.db();
        gPeak.update(db, gAudioMs);
        gHistory.push(db, gAudioMs);
        gAlarm.update(db, static_cast<float>(gAlarmDb - gCal), gAudioMs);
    }

    const uint32_t now = millis();
    if (gLastFrameMs == 0 || now - gLastFrameMs >= kFrameMs) {
        gLastFrameMs = now;
        return true;
    }
    return false;
}

void noise_app::draw(LovyanGFX &g)
{
    const uint32_t now = millis();
    const bool flash = gAlarm.active() && (now / 150) % 2 == 0;
    const uint16_t bg = flash ? kFlashBg : TFT_BLACK;
    if (flash) g.fillScreen(bg);

    // ── 顶栏 ──
    g.setTextColor(TFT_WHITE, bg);
    g.drawString(gAlarm.active() ? "NOISE  ALARM" : "NOISE", 0, kTitleY);

    char right[32];
    if (now < gCalShownUntilMs) {
        std::snprintf(right, sizeof(right), "cal %d", gCal);
    } else if (gHistory.size() > 0) {
        std::snprintf(right, sizeof(right), "max %d  alarm %d",
                      static_cast<int>(std::lround(shown(gHistory.maxDb()))), gAlarmDb);
    } else {
        std::snprintf(right, sizeof(right), "alarm %d", gAlarmDb);
    }
    g.setTextColor(TFT_DARKGREY, bg);
    g.drawString(right, kScreenW - static_cast<int>(std::strlen(right)) * kCharW, kTitleY);

    g.drawString("SPC reset =-alarm []cal `back", 0, kHintY);

    if (!gMicOk) {
        g.setTextColor(TFT_WHITE, bg);
        g.setTextSize(2);
        g.drawString("mic error", 8, kNumY + 16);
        g.setTextSize(1);
        return;
    }

    // ── 大数字：最近一个 250ms 箱的最大值，一秒只变 4 次，看得清 ──
    char num[8];
    const bool have = gHistory.size() > 0;
    const float now_db = shown(gHistory.lastComplete());
    if (have) {
        std::snprintf(num, sizeof(num), "%d", static_cast<int>(std::lround(now_db)));
    } else {
        std::snprintf(num, sizeof(num), "--");
    }
    g.setTextColor(TFT_WHITE, bg);
    g.setTextSize(3);
    g.setTextDatum(top_right);
    g.drawString(num, 80, kNumY);
    g.setTextDatum(top_left);
    g.setTextSize(1);

    g.setTextColor(TFT_DARKGREY, bg);
    g.drawString("dB", 88, kNumY + 2);
    if (have) {
        g.setTextColor(grey(200), bg);
        g.setTextSize(2);
        g.drawString(noisemeter::label(now_db), 88, kNumY + 18);
        g.setTextSize(1);
    }

    // ── 实时音量条 ──
    const int fill = static_cast<int>(noisemeter::fraction(shown(gLevel.db()), kLoDb, kHiDb) * kScreenW);
    const int peakX = static_cast<int>(noisemeter::fraction(shown(gPeak.db()), kLoDb, kHiDb) * (kScreenW - 2));
    const int alarmX = static_cast<int>(
        noisemeter::fraction(static_cast<float>(gAlarmDb), kLoDb, kHiDb) * (kScreenW - 1));
    g.fillRect(0, kBarY, kScreenW, kBarH, grey(40));
    if (fill > 0) g.fillRect(0, kBarY, fill, kBarH, grey(190));
    g.fillRect(peakX, kBarY, 2, kBarH, TFT_WHITE);
    g.drawFastVLine(alarmX, kBarY - 3, kBarH + 6, grey(120));

    drawHistory(g);
}

bool noise_app::handleKey(char c)
{
    switch (c) {
        case '`':
            return false;  // 回菜单（调用方负责调 end()）

        case ' ':
            // 清零最大值、峰值和曲线。实时值不动：它本来就跟着声音走
            gPeak.reset();
            gHistory.reset();
            gAlarm.reset();
            break;

        // 这一页不出声，= - 空着，拿来调报警线；每次 5 dB
        case '=':
            if (gAlarmDb < 110) gAlarmDb += 5;
            saveSettings();
            break;
        case '-':
            if (gAlarmDb > 40) gAlarmDb -= 5;
            saveSettings();
            break;

        // 校准：对着手机分贝仪，调到读数一致。每次 1 dB
        case ']':
            if (gCal < 200) ++gCal;
            gCalShownUntilMs = millis() + 2000;
            saveSettings();
            break;
        case '[':
            if (gCal > 0) --gCal;
            gCalShownUntilMs = millis() + 2000;
            saveSettings();
            break;

        default:
            break;
    }
    return true;
}
