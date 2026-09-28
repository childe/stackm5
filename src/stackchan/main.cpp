// StackChan Core + Faces Bottom3 / Keyboard3 背词机。
//
// 复用 lib/vocab 的词表与解析器，但这里采用「看释义，拼单词」的主动回忆：
// Keyboard3 的 Normal 模式由板载 MCU 映射为 ASCII；我们直接消费每个 I2C
// 事件，不经过只报告“键值变化”的封装层，因此连续相同字母也不会丢失。

#include <M5Faces.h>
#include <M5Unified.h>
#include <breakout.h>
#include <esp_random.h>
#include <pomodoro.h>
#include <texted.h>
#include <vocab.h>
#include <wordlist.h>

#include "ipa_text.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr uint8_t kKeyboardAddress = 0x08;
constexpr uint32_t kKeyboardI2cHz = 400000;
constexpr size_t kAnswerMaxChars = 30;
constexpr int kHeaderH = 26;
constexpr int kBodyLineH = 24;
constexpr int kInputH = 30;
constexpr uint8_t kKeyboardKeyRegister = 0x00;

enum class Page { Home, Vocab, Focus, Breakout, KeyTest };
enum class VocabScreen { Prompt, Result, Example };

M5Faces_Keyboard3 gKeyboard;
pomodoro::Timer gTimer;
breakout::Game gBreakout(320, 240);
vocab::WordList gWords;
size_t gIndex = 0;
Page gPage = Page::Home;
VocabScreen gVocabScreen = VocabScreen::Prompt;
std::string gAnswer;
std::string gMessage;
bool gHintShown = false;
bool gKeyboardReady = false;
bool gDirty = true;
M5Canvas *gBreakoutCanvas = nullptr;
uint32_t gDrawnFocusSeconds = UINT32_MAX;
uint8_t gLastRawKey = 0;
uint32_t gKeyEventCount = 0;
float gTiltZero = 0.0f;
float gTiltFiltered = 0.0f;
uint32_t gLastBreakoutMs = 0;
uint32_t gAttempts = 0;
uint32_t gCorrect = 0;

void drawText(const char *text, int x, int y, uint16_t color, uint8_t size = 1)
{
    M5.Display.setFont(&fonts::AsciiFont8x16);
    M5.Display.setTextSize(size);
    M5.Display.setTextColor(color, TFT_BLACK);
    M5.Display.drawString(text, x, y);
    M5.Display.setTextSize(1);
}

void drawGameText(LovyanGFX &g, const char *text, int x, int y, uint16_t color, uint8_t size = 1)
{
    g.setFont(&fonts::AsciiFont8x16);
    g.setTextSize(size);
    g.setTextColor(color, TFT_BLACK);
    g.drawString(text, x, y);
    g.setTextSize(1);
}

void drawWrapped(const std::string &text, int y, size_t columns, size_t maxLines, uint16_t color)
{
    M5.Display.setFont(&fonts::FreeMono12pt7b);
    M5.Display.setTextColor(color, TFT_BLACK);

    const std::vector<texted::Line> lines = texted::wrapLines(text, columns);
    for (size_t i = 0; i < lines.size() && i < maxLines; ++i) {
        const texted::Line &line = lines[i];
        M5.Display.drawString(text.substr(line.start, line.len).c_str(), 8,
                              y + static_cast<int>(i) * kBodyLineH);
    }
}

std::string lowerAscii(std::string text)
{
    for (char &c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

void pickWord()
{
    if (gWords.words.empty()) return;

    gIndex = esp_random() % gWords.words.size();
    gAnswer.clear();
    gMessage.clear();
    gHintShown = false;
    gVocabScreen = VocabScreen::Prompt;
    gDirty = true;
}

void checkAnswer()
{
    if (gAnswer.empty()) {
        gMessage = "type an answer first";
        gDirty = true;
        return;
    }

    ++gAttempts;
    const vocab::Word &word = gWords.words[gIndex];
    if (lowerAscii(gAnswer) == lowerAscii(word.word)) {
        ++gCorrect;
        gMessage = "CORRECT";
    } else {
        gMessage = "NOT QUITE";
    }
    gVocabScreen = VocabScreen::Result;
    gDirty = true;
}

void drawVocabHeader()
{
    M5.Display.fillRect(0, 0, M5.Display.width(), kHeaderH, TFT_NAVY);
    drawText("VOCAB", 8, 5, TFT_WHITE);

    char score[32];
    std::snprintf(score, sizeof(score), "%u/%u", static_cast<unsigned>(gCorrect),
                  static_cast<unsigned>(gAttempts));
    M5.Display.setFont(&fonts::AsciiFont8x16);
    M5.Display.setTextColor(TFT_CYAN, TFT_NAVY);
    M5.Display.drawRightString(score, M5.Display.width() - 8, 5);
}

void drawPrompt()
{
    const vocab::Word &word = gWords.words[gIndex];

    drawText("TYPE THE WORD", 8, kHeaderH + 10, TFT_CYAN);
    drawText("0 home", 262, kHeaderH + 10, TFT_DARKGREY);
    drawWrapped(word.definition, kHeaderH + 34, 22, 4, TFT_WHITE);

    if (!gMessage.empty()) {
        drawText(gMessage.c_str(), 8, 158, TFT_ORANGE);
    } else if (gHintShown) {
        char hint[32];
        std::snprintf(hint, sizeof(hint), "hint: %c%s", word.word[0],
                      word.word.size() > 1 ? "..." : "");
        drawText(hint, 8, 158, TFT_DARKGREY);
    } else {
        drawText("touch definition for hint", 8, 158, TFT_DARKGREY);
    }
    const int inputY = M5.Display.height() - 64;
    M5.Display.drawRoundRect(7, inputY - 4, M5.Display.width() - 14, kInputH, 4, TFT_DARKGREY);

    // 长单词也允许完整输入；显示时只保留末尾，光标始终可见。
    const size_t visibleChars = 18;
    const char *visible = gAnswer.size() > visibleChars ? gAnswer.c_str() + gAnswer.size() - visibleChars
                                                        : gAnswer.c_str();
    drawText(visible, 14, inputY + 2, TFT_WHITE, 2);
    drawText("ENTER check   SPC hint   BS erase", 8, M5.Display.height() - 27, TFT_DARKGREY);
}

void drawResult()
{
    const vocab::Word &word = gWords.words[gIndex];
    const bool correct = gMessage == "CORRECT";

    drawText(gMessage.c_str(), 8, kHeaderH + 10, correct ? TFT_GREEN : TFT_ORANGE, 2);
    drawText(word.word.c_str(), 8, kHeaderH + 47, TFT_WHITE, 2);
    ipa_text::draw(M5.Display, word.phonetic.c_str(), 8, kHeaderH + 82, TFT_CYAN);

    drawWrapped(word.definition, kHeaderH + 108, 22, 3, TFT_LIGHTGREY);

    drawText(word.example.empty() ? "ENTER / SPACE / touch: next"
                                  : "ENTER / SPACE / touch: example",
             8, M5.Display.height() - 22, TFT_DARKGREY);
}

void drawExample()
{
    const vocab::Word &word = gWords.words[gIndex];

    drawText(word.word.c_str(), 8, kHeaderH + 10, TFT_CYAN, 2);
    if (word.example.empty()) {
        drawText("(no example)", 8, kHeaderH + 54, TFT_DARKGREY);
    } else {
        drawWrapped(word.example, kHeaderH + 54, 22, 5, TFT_WHITE);
    }
    drawText("ENTER / SPACE / touch: next", 8, M5.Display.height() - 22, TFT_DARKGREY);
}

void drawHome()
{
    drawText("STACKCHAN", 8, 22, TFT_WHITE, 2);
    drawText("1  VOCAB", 20, 82, TFT_CYAN, 2);
    drawText("2  FOCUS", 20, 128, TFT_GREEN, 2);
    drawText("3  BRICKOUT", 20, 174, TFT_MAGENTA, 2);
    drawText("4  KEY TEST", 20, 202, TFT_YELLOW, 2);
    drawText("V/F/G/K or touch", 8, 220, TFT_DARKGREY);
}

void drawFocusTime(uint32_t seconds)
{
    // 只有数字区域更新，不能每秒 fillScreen()；CoreS3 LCD 全屏重画会肉眼闪烁。
    M5.Display.fillRect(32, 66, 260, 68, TFT_BLACK);

    char time[8];
    std::snprintf(time, sizeof(time), "%02u:%02u", static_cast<unsigned>(seconds / 60),
                  static_cast<unsigned>(seconds % 60));
    drawText(time, 36, 70, TFT_WHITE, 4);
    gDrawnFocusSeconds = seconds;
}

void drawFocus()
{
    const bool focus = gTimer.phase() == pomodoro::Phase::Focus;
    const uint16_t color = focus ? TFT_GREEN : TFT_CYAN;
    const uint32_t seconds = gTimer.remainingSeconds(millis());

    drawText(focus ? "FOCUS" : "BREAK", 8, 18, color, 2);

    drawFocusTime(seconds);

    char completed[32];
    std::snprintf(completed, sizeof(completed), "today %u completed",
                  static_cast<unsigned>(gTimer.completedFocuses()));
    drawText(completed, 8, 160, TFT_DARKGREY);
    drawText(gTimer.isRunning() ? "SPACE pause" : "SPACE start", 8, 190, color);
    drawText("N skip   R reset   0 home", 8, 216, TFT_DARKGREY);
}

void drawBreakoutTo(LovyanGFX &g)
{
    const breakout::State state = gBreakout.state();
    g.fillScreen(TFT_BLACK);

    for (size_t i = 0; i < gBreakout.bricks().size(); ++i) {
        const breakout::Brick &brick = gBreakout.bricks()[i];
        const uint16_t colors[] = {TFT_RED, TFT_ORANGE, TFT_YELLOW, TFT_GREEN};
        g.fillRoundRect(static_cast<int>(brick.rect.x), static_cast<int>(brick.rect.y),
                        static_cast<int>(brick.rect.w), static_cast<int>(brick.rect.h), 2,
                        colors[brick.row % (sizeof(colors) / sizeof(colors[0]))]);
    }

    const breakout::Rect &paddle = gBreakout.paddle();
    g.fillRoundRect(static_cast<int>(paddle.x), static_cast<int>(paddle.y), static_cast<int>(paddle.w),
                    static_cast<int>(paddle.h), 3, TFT_CYAN);

    const breakout::Ball &ball = gBreakout.ball();
    g.fillCircle(static_cast<int>(ball.x), static_cast<int>(ball.y), static_cast<int>(ball.radius), TFT_WHITE);

    char score[24];
    std::snprintf(score, sizeof(score), "score %u", static_cast<unsigned>(gBreakout.score()));
    drawGameText(g, score, 4, 4, TFT_DARKGREY);

    if (state == breakout::State::Ready) {
        drawGameText(g, "TILT TO AIM", 74, 104, TFT_WHITE, 2);
        drawGameText(g, "SPACE launch  C calibrate  0 home", 8, 202, TFT_DARKGREY);
    } else if (state == breakout::State::Won) {
        drawGameText(g, "YOU WIN", 94, 104, TFT_GREEN, 2);
        drawGameText(g, "SPACE restart  C calibrate  0 home", 8, 202, TFT_DARKGREY);
    } else if (state == breakout::State::Lost) {
        drawGameText(g, "BALL LOST", 74, 104, TFT_ORANGE, 2);
        drawGameText(g, "SPACE restart  C calibrate  0 home", 8, 202, TFT_DARKGREY);
    } else {
        drawGameText(g, "C calibrate  0 home", 8, 202, TFT_DARKGREY);
    }
}

void drawBreakout()
{
    if (gBreakoutCanvas) {
        drawBreakoutTo(*gBreakoutCanvas);
        gBreakoutCanvas->pushSprite(0, 0);
    } else {
        drawBreakoutTo(M5.Display);
    }
}

void drawKeyTest()
{
    drawText("KEY TEST", 8, 18, TFT_YELLOW, 2);
    drawText("press any keyboard key", 8, 56, TFT_DARKGREY);

    char raw[32];
    std::snprintf(raw, sizeof(raw), "raw  0x%02X  %u", static_cast<unsigned>(gLastRawKey),
                  static_cast<unsigned>(gLastRawKey));
    drawText(raw, 8, 96, TFT_WHITE, 2);

    const char *meaning = "waiting";
    if (gKeyEventCount > 0) {
        if (gLastRawKey == 0x1B) {
            meaning = "ESC";
        } else if (gLastRawKey == '\r') {
            meaning = "CR / ENTER";
        } else if (gLastRawKey == '\n') {
            meaning = "LF";
        } else if (gLastRawKey == '\b') {
            meaning = "BACKSPACE";
        } else if (gLastRawKey >= 0x20 && gLastRawKey <= 0x7E) {
            static char printable[16];
            std::snprintf(printable, sizeof(printable), "ASCII '%c'", gLastRawKey);
            meaning = printable;
        } else {
            meaning = "extended / control";
        }
    }
    drawText(meaning, 8, 138, gLastRawKey == 0x1B ? TFT_GREEN : TFT_CYAN, 2);

    char count[24];
    std::snprintf(count, sizeof(count), "events %u", static_cast<unsigned>(gKeyEventCount));
    drawText(count, 8, 182, TFT_DARKGREY);
    drawText("touch screen: home", 8, 216, TFT_DARKGREY);
}

void draw()
{
    M5.Display.fillScreen(TFT_BLACK);
    gDrawnFocusSeconds = UINT32_MAX;

    switch (gPage) {
        case Page::Home:
            drawHome();
            break;
        case Page::Focus:
            drawFocus();
            break;
        case Page::Breakout:
            drawBreakout();
            break;
        case Page::KeyTest:
            drawKeyTest();
            break;
        case Page::Vocab:
            drawVocabHeader();
            if (!gWords.error.ok) {
                char error[80];
                std::snprintf(error, sizeof(error), "wordlist line %u: %s",
                              static_cast<unsigned>(gWords.error.line), gWords.error.reason);
                drawText(error, 8, 48, TFT_RED);
                break;
            }
            if (gWords.words.empty()) {
                drawText("wordlist is empty", 8, 48, TFT_RED);
                break;
            }
            switch (gVocabScreen) {
                case VocabScreen::Prompt:
                    drawPrompt();
                    break;
                case VocabScreen::Result:
                    drawResult();
                    break;
                case VocabScreen::Example:
                    drawExample();
                    break;
            }
            break;
    }

    if (!gKeyboardReady) {
        M5.Display.fillRect(0, M5.Display.height() - 46, M5.Display.width(), 46, TFT_MAROON);
        drawText("Keyboard3 missing: touch only", 8, M5.Display.height() - 40, TFT_WHITE);
        drawText("check Bottom3 / I2C 0x08", 8, M5.Display.height() - 21, TFT_WHITE);
    }
}

void advanceFromResult()
{
    if (gWords.words[gIndex].example.empty()) {
        pickWord();
    } else {
        gVocabScreen = VocabScreen::Example;
        gDirty = true;
    }
}

void openVocab()
{
    if (gWords.error.ok && !gWords.words.empty()) pickWord();
    gPage = Page::Vocab;
    gDirty = true;
}

void calibrateBreakout()
{
    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    if (M5.Imu.getAccel(&ax, &ay, &az)) {
        gTiltZero = ax;
        gTiltFiltered = 0.0f;
    }
}

void updateBreakout()
{
    const uint32_t now = millis();
    const float dt = std::min((now - gLastBreakoutMs) / 1000.0f, 0.05f);
    gLastBreakoutMs = now;

    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    if (M5.Imu.getAccel(&ax, &ay, &az)) {
        const float rawTilt = (ax - gTiltZero) * 4.0f;
        gTiltFiltered += (std::clamp(rawTilt, -1.0f, 1.0f) - gTiltFiltered) * 0.14f;
        const float deadZone = 0.06f;
        const float magnitude = std::abs(gTiltFiltered);
        const float tilt = magnitude <= deadZone
                               ? 0.0f
                               : std::copysign((magnitude - deadZone) / (1.0f - deadZone), gTiltFiltered);
        gBreakout.setTilt(-tilt);
    }
    gBreakout.update(dt);
}

void handleFocusKey(char c)
{
    const uint32_t now = millis();
    switch (c) {
        case ' ':
        case '\n':
            gTimer.toggle(now);
            break;
        case 'n':
        case 'N':
            gTimer.skip();
            break;
        case 'r':
        case 'R':
            gTimer.reset();
            break;
        case '0':
            gPage = Page::Home;
            break;
        default:
            return;
    }
    gDirty = true;
}

void handleBreakoutKey(char c)
{
    switch (c) {
        case '0':
            gPage = Page::Home;
            break;
        case 'c':
        case 'C':
            calibrateBreakout();
            break;
        case ' ':
        case '\n':
            if (gBreakout.state() == breakout::State::Ready) {
                gBreakout.launch();
            } else if (gBreakout.state() == breakout::State::Won || gBreakout.state() == breakout::State::Lost) {
                gBreakout.reset();
                calibrateBreakout();
            }
            break;
        default:
            return;
    }
    gDirty = true;
}

void handleVocabKey(char c)
{
    if (c == '0') {
        gPage = Page::Home;
        gDirty = true;
        return;
    }
    if (gWords.words.empty() || c == '\0') return;

    if (gVocabScreen == VocabScreen::Result) {
        if (c == '\n' || c == ' ') advanceFromResult();
        return;
    }
    if (gVocabScreen == VocabScreen::Example) {
        if (c == '\n' || c == ' ') pickWord();
        return;
    }

    if (c == '\n') {
        checkAnswer();
    } else if (c == '\b' || c == 0x7F) {
        if (!gAnswer.empty()) {
            gAnswer.pop_back();
            gDirty = true;
        }
    } else if (c == ' ') {
        gHintShown = true;
        gDirty = true;
    } else if (std::isalpha(static_cast<unsigned char>(c)) && gAnswer.size() < kAnswerMaxChars) {
        gAnswer.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        gMessage.clear();
        gDirty = true;
    }
}

void handleKey(char c)
{
    if (c == '\0') return;

    switch (gPage) {
        case Page::Home:
            if (c == '1' || c == 'v' || c == 'V') {
                openVocab();
            } else if (c == '2' || c == 'f' || c == 'F') {
                gPage = Page::Focus;
                gDirty = true;
            } else if (c == '3' || c == 'g' || c == 'G') {
                gBreakout.reset();
                calibrateBreakout();
                gLastBreakoutMs = millis();
                gPage = Page::Breakout;
                gDirty = true;
            } else if (c == '4' || c == 'k' || c == 'K') {
                gPage = Page::KeyTest;
                gDirty = true;
            }
            break;
        case Page::Vocab:
            handleVocabKey(c);
            break;
        case Page::Focus:
            handleFocusKey(c);
            break;
        case Page::Breakout:
            handleBreakoutKey(c);
            break;
        case Page::KeyTest:
            // 特意不把任何键解释为返回，让每个组合都能被观察到。
            break;
    }
}

bool readKeyboardByte(uint8_t *value)
{
    return M5.In_I2C.readRegister(kKeyboardAddress, kKeyboardKeyRegister, value, 1,
                                  kKeyboardI2cHz);
}

void handleKeyboardEvent()
{
    uint8_t raw = 0;
    if (!readKeyboardByte(&raw) || raw == 0) return;

    if (gPage == Page::KeyTest) {
        gLastRawKey = raw;
        ++gKeyEventCount;
        gDirty = true;
        return;
    }

    // Keyboard3 对一次 Enter 连续发送 CR、LF。第一字节到达时立即读走 LF，
    // 避免下一轮 loop 把同一次 Enter 当成第二次提交。
    if (raw == '\r') {
        uint8_t lf = 0;
        readKeyboardByte(&lf);
        handleKey('\n');
        return;
    }
    if (raw == '\n') return;  // 防御：若 LF 留在队列中，不重复提交。

    handleKey(static_cast<char>(raw));
}

void handleTouch()
{
    if (M5.Touch.getCount() == 0 || !M5.Touch.getDetail().wasClicked()) return;

    const int y = M5.Touch.getDetail().y;
    if (gPage == Page::Home) {
        if (y < 106) {
            openVocab();
        } else if (y < 154) {
            gPage = Page::Focus;
            gDirty = true;
        } else if (y < 190) {
            gBreakout.reset();
            calibrateBreakout();
            gLastBreakoutMs = millis();
            gPage = Page::Breakout;
            gDirty = true;
        } else {
            gPage = Page::KeyTest;
            gDirty = true;
        }
        return;
    }

    if (gPage == Page::KeyTest) {
        gPage = Page::Home;
        gDirty = true;
        return;
    }

    if (gPage == Page::Breakout) {
        if (y >= 200) {
            handleBreakoutKey(' ');
        } else {
            calibrateBreakout();
            gDirty = true;
        }
        return;
    }

    if (gPage == Page::Focus) {
        if (y >= 180) {
            gTimer.toggle(millis());
        } else {
            gTimer.skip();
        }
        gDirty = true;
        return;
    }

    if (!gKeyboardReady) {
        if (gVocabScreen == VocabScreen::Prompt) {
            gMessage = "ANSWER";
            gVocabScreen = VocabScreen::Result;
        } else {
            pickWord();
        }
        gDirty = true;
        return;
    }

    if (gVocabScreen == VocabScreen::Result) {
        advanceFromResult();
    } else if (gVocabScreen == VocabScreen::Example) {
        pickWord();
    } else if (y >= M5.Display.height() - 80) {
        checkAnswer();
    } else {
        gHintShown = true;
        gDirty = true;
    }
}

}  // namespace

void setup()
{
    auto config = M5.config();
    M5.begin(config);
    M5.Display.setRotation(1);
    M5.Display.setBrightness(120);

    // 320x240x16bpp 约 150KB，放进 StackChan Core 的 PSRAM 以消除游戏逐帧重画闪烁。
    gBreakoutCanvas = new M5Canvas(&M5.Display);
    gBreakoutCanvas->setColorDepth(16);
    gBreakoutCanvas->setPsram(true);
    if (!gBreakoutCanvas->createSprite(M5.Display.width(), M5.Display.height())) {
        delete gBreakoutCanvas;
        gBreakoutCanvas = nullptr;
    }

    gWords = vocab::parse(vocab::kRawWords);
    if (gWords.error.ok && !gWords.words.empty()) pickWord();

    // Normal 模式由 Keyboard3 固件处理层、符号和大小写映射。输入事件直接经 I2C
    // 读取，不能调用 gKeyboard.update()：它只报告“键值变化”，会漏掉连续同一字母。
    gKeyboardReady = gKeyboard.begin(&M5.In_I2C, kKeyboardAddress, kKeyboardI2cHz) == M5FACES_OK;
    if (gKeyboardReady && gKeyboard.setMode(M5FACES_MODE_NORMAL) != M5FACES_OK) {
        gKeyboardReady = false;
    }

    draw();
}

void loop()
{
    M5.update();

    if (gKeyboardReady) handleKeyboardEvent();
    handleTouch();

    if (gTimer.tick(millis())) {
        gDirty = true;
    }

    if (gPage == Page::Breakout) {
        updateBreakout();
        drawBreakout();
    }

    if (gPage == Page::Focus && !gDirty) {
        const uint32_t seconds = gTimer.remainingSeconds(millis());
        if (seconds != gDrawnFocusSeconds) drawFocusTime(seconds);
    }

    if (gDirty) {
        draw();
        gDirty = false;
    }
    delay(12);
}
