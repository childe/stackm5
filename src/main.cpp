/*
 * Cardputer-Adv —— 两个 app 共存于一个固件，开机在菜单页选
 *
 * 菜单页
 *   1           简谱演奏器
 *   2           背单词
 *
 * ── 以下是简谱演奏器 ────────────────────────────────────────
 *
 * 曲库页
 *   ;  .        上一首 / 下一首
 *   ⏎           打开编辑页
 *   空格         直接播放（不进编辑页）
 *   n           新建一首
 *   ⌫           删除（按两次确认）
 *
 * 编辑页
 *   1-7 0       音级 / 休止符（按下即试听）
 *   ' ,         高八度 / 低八度
 *   - / . # b   增时线 / 减时线 / 附点 / 升 / 降
 *   空格         分隔音符
 *   [  ]        光标左移 / 右移      fn + [ ] 跳行首 / 行尾
 *   ⌫           退格
 *   ⏎           播放；播放中再按停止
 *   =           调号 / 速度设置
 *   `           存盘并返回曲库页
 *
 * 设置页
 *   [  ]        切换调号（按下即用新调号试听 do）
 *   ;  .        速度 +5 / -5
 *   ⏎ 或 `       返回编辑页
 *
 * 改动后 1.5 秒无操作自动存盘（Flash 有擦写寿命，不能每敲一个字符就写一次）。
 */
#include <Arduino.h>
#include <M5Cardputer.h>
#include <jianpu.h>
#include <songs.h>
#include <texted.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "library.h"
#include "player.h"
#include "remote_app.h"
#include "vocab_app.h"

// 屏幕旋转后 240x135；AsciiFont8x16 是 8x16 严格等宽 → 正好 30 列
static constexpr int kCharW = 8;
static constexpr int kCharH = 16;
static constexpr size_t kCols = 30;
static constexpr size_t kVisibleRows = 4;

static constexpr int kScreenW = 240;
static constexpr int kScreenH = 135;

static constexpr int kTitleY = 0;
static constexpr int kBodyY = 20;
static constexpr int kStatusY = 90;
static constexpr int kHintY = 112;

static constexpr size_t kMaxChars = 1024;  // Note::srcPos 是 uint16_t
static constexpr uint32_t kAutosaveMs = 1500;
static constexpr uint32_t kPreviewMs = 140;

static const char *kDefaultHeader = "1=C 4/4 120";

// 内置示例的版本。加了新谱子就把这个数字 +1，老设备下次开机会自动补写。
static constexpr int kSeedVersion = 2;

// 12 个调号，覆盖全部半音
static const char *kKeyNames[] = {"C", "C#", "D", "Eb", "E", "F",
                                  "F#", "G", "Ab", "A", "Bb", "B"};
static constexpr size_t kKeyCount = sizeof(kKeyNames) / sizeof(kKeyNames[0]);

enum class Page { Menu, Library, Editor, Settings, Vocab, Remote };

static Page gPage = Page::Menu;
static Player gPlayer;
static bool gDirty = true;
static bool gWasPlaying = false;
static int gLastPlayIdx = -1;

// 离屏画布。整块推上去，避免播放高亮时每秒重画好几次造成闪烁。
static M5Canvas *gCanvas = nullptr;

// ── 编辑页状态 ──────────────────────────────────────────────
static texted::Buffer gBuf;
static std::string gHeaderLine = kDefaultHeader;
static uint8_t gCurrentId = 0;
static jianpu::Score gScore;
static size_t gBodyOffset = 0;  // 正文在完整文本里的起始下标
static bool gUnsaved = false;
static uint32_t gLastEditMs = 0;
static bool gPlayingOwnScore = false;  // 播的是编辑页这首（决定要不要高亮）

// 头部行拆出来的三项，设置页直接改这些
static size_t gKeyIdx = 0;
static std::string gTimeSig = "4/4";
static int gBpm = 120;

// ── 曲库页状态 ──────────────────────────────────────────────
static std::vector<library::Entry> gEntries;
static size_t gSel = 0;
static bool gConfirmDelete = false;

// ───────────────────────────────────────────────────────────

// 简谱用得上的字符。其他键一律忽略 —— 小键盘误触很容易，与其让你敲出一个
// 解析不了的谱子，不如直接不收。
static bool isJianpuChar(char c)
{
    if (c == '\0') return false;
    return std::strchr("01234567',-/.#b |", c) != nullptr;
}

static std::string fullText()
{
    return gHeaderLine + "\n" + gBuf.text();
}

static void reparse()
{
    const std::string t = fullText();
    gBodyOffset = gHeaderLine.size() + 1;
    gScore = jianpu::parse(t.c_str(), t.size());
}

static void saveNow()
{
    if (gCurrentId == 0) return;
    library::save(gCurrentId, fullText());
    gUnsaved = false;
}

static void touched()
{
    gUnsaved = true;
    gLastEditMs = millis();
    reparse();
    gDirty = true;
}

// 把 gHeaderLine 拆成 调号 / 拍号 / 速度 三项
static void headerToState()
{
    gKeyIdx = 0;
    gTimeSig = "4/4";
    gBpm = 120;

    size_t i = 0;
    while (i < gHeaderLine.size()) {
        while (i < gHeaderLine.size() && gHeaderLine[i] == ' ') ++i;
        const size_t start = i;
        while (i < gHeaderLine.size() && gHeaderLine[i] != ' ') ++i;
        if (i == start) continue;

        const std::string tok = gHeaderLine.substr(start, i - start);

        if (tok.size() > 2 && tok[0] == '1' && tok[1] == '=') {
            const std::string name = tok.substr(2);
            for (size_t k = 0; k < kKeyCount; ++k) {
                if (name == kKeyNames[k]) {
                    gKeyIdx = k;
                    break;
                }
            }
        } else if (tok.find('/') != std::string::npos) {
            gTimeSig = tok;  // 拍号原样保留，不然改速度会把它弄丢
        } else {
            bool digits = true;
            for (const char c : tok) {
                if (c < '0' || c > '9') {
                    digits = false;
                    break;
                }
            }
            if (digits) gBpm = std::atoi(tok.c_str());
        }
    }
}

static void stateToHeader()
{
    char buf[40];
    std::snprintf(buf, sizeof(buf), "1=%s %s %d", kKeyNames[gKeyIdx], gTimeSig.c_str(), gBpm);
    gHeaderLine = buf;
}

// 试听紧贴在光标左边的那个音符。
//
// 为什么是「回看光标左边」而不是「按下哪个数字响哪个音」：修饰符是后敲的。
// 你想输入高音 do，按键顺序是 1 然后 '，按下 1 的瞬间程序还不知道后面要跟 '。
// 回看的做法让 1 先按中音响一下、敲完 ' 再按高音响一下 —— 你能听到修正过程。
static void previewNoteBeforeCursor()
{
    if (gPlayer.isPlaying()) return;  // 正在放整曲，别抢喇叭
    if (!gScore.error.ok) return;

    const size_t want = gBodyOffset + gBuf.cursor();
    for (const jianpu::Note &n : gScore.notes) {
        if (static_cast<size_t>(n.srcPos) + n.srcLen == want) {
            const float f = jianpu::noteToFreq(n, gScore.header);
            if (f > 0.0f) M5Cardputer.Speaker.tone(f, kPreviewMs);
            return;
        }
    }
}

static void previewTonic()
{
    if (gPlayer.isPlaying()) return;

    // 直接借解析器算：造一段只有 "1" 的谱子，套上当前调号
    const std::string probe = std::string("1=") + kKeyNames[gKeyIdx] + " 4/4 120\n1";
    const jianpu::Score s = jianpu::parse(probe.c_str(), probe.size());
    if (s.error.ok && !s.notes.empty()) {
        const float f = jianpu::noteToFreq(s.notes[0], s.header);
        if (f > 0.0f) M5Cardputer.Speaker.tone(f, 200);
    }
}

static void refreshEntries()
{
    gEntries = library::list();
    if (gSel >= gEntries.size()) gSel = gEntries.empty() ? 0 : gEntries.size() - 1;
}

static void openEditor(uint8_t id)
{
    std::string text;
    if (!library::load(id, text)) text.clear();

    const size_t skip = jianpu::headerPrefixLen(text.c_str(), text.size());
    if (skip > 0) {
        gHeaderLine = text.substr(0, skip);
        while (!gHeaderLine.empty() &&
               (gHeaderLine.back() == '\n' || gHeaderLine.back() == '\r')) {
            gHeaderLine.pop_back();
        }
    } else {
        gHeaderLine = kDefaultHeader;
    }

    headerToState();
    gBuf.setText(text.substr(skip));
    gCurrentId = id;
    gUnsaved = false;
    gPage = Page::Editor;
    library::setLastOpened(id);
    reparse();
    gDirty = true;
}

static void leaveEditor()
{
    saveNow();
    gPage = Page::Library;
    refreshEntries();

    for (size_t i = 0; i < gEntries.size(); ++i) {
        if (gEntries[i].id == gCurrentId) {
            gSel = i;
            break;
        }
    }
    gConfirmDelete = false;
    gDirty = true;
}

static void playById(uint8_t id)
{
    std::string text;
    if (!library::load(id, text)) return;

    const jianpu::Score s = jianpu::parse(text.c_str(), text.size());
    if (s.error.ok && !s.notes.empty()) {
        gPlayingOwnScore = false;
        gPlayer.start(s);
    }
    gDirty = true;
}

// 补写内置示例。已经有内容一样的曲子就跳过，避免升级固件后出现重复项。
static void seedExamples()
{
    if (library::seedVersion() >= kSeedVersion) return;

    const std::vector<library::Entry> existing = library::list();

    // 先把现有内容都读出来，免得每首示例都重读一遍整个曲库
    std::vector<std::string> existingText;
    existingText.reserve(existing.size());
    for (const library::Entry &e : existing) {
        std::string t;
        if (library::load(e.id, t)) existingText.push_back(t);
    }

    for (size_t i = 0; i < songs::kBuiltinCount; ++i) {
        const std::string text = songs::fullTextOf(songs::kBuiltins[i]);

        bool duplicate = false;
        for (const std::string &t : existingText) {
            if (t == text) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && library::createNew(text) > 0) existingText.push_back(text);
    }

    library::setSeedVersion(kSeedVersion);
}

// ── 绘制 ────────────────────────────────────────────────────

static void drawRightAligned(LovyanGFX &g, const char *s, int y, uint16_t color)
{
    g.setTextColor(color, TFT_BLACK);
    g.drawString(s, kScreenW - static_cast<int>(std::strlen(s)) * kCharW, y);
}

static void drawLibrary(LovyanGFX &g)
{
    g.setTextColor(gPlayer.isPlaying() ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
    g.drawString(gPlayer.isPlaying() ? "PLAYING" : "LIBRARY", 0, kTitleY);

    char count[16];
    std::snprintf(count, sizeof(count), "%u songs", static_cast<unsigned>(gEntries.size()));
    drawRightAligned(g, count, kTitleY, TFT_DARKGREY);

    if (gEntries.empty()) {
        g.setTextColor(TFT_DARKGREY, TFT_BLACK);
        g.drawString("empty - press n to add", 0, kBodyY);
    } else {
        const size_t first = (gSel >= kVisibleRows) ? gSel - (kVisibleRows - 1) : 0;

        for (size_t i = 0; i < kVisibleRows && first + i < gEntries.size(); ++i) {
            const library::Entry &e = gEntries[first + i];
            const bool selected = (first + i == gSel);

            char row[kCols + 1];
            std::snprintf(row, sizeof(row), "%c %02u %s", selected ? '>' : ' ',
                          static_cast<unsigned>(e.id), e.preview.c_str());

            g.setTextColor(selected ? TFT_CYAN : TFT_DARKGREY, TFT_BLACK);
            g.drawString(row, 0, kBodyY + static_cast<int>(i) * kCharH);
        }
    }

    if (gConfirmDelete && !gEntries.empty()) {
        char msg[40];
        std::snprintf(msg, sizeof(msg), "DEL again to erase %02u",
                      static_cast<unsigned>(gEntries[gSel].id));
        g.setTextColor(TFT_RED, TFT_BLACK);
        g.drawString(msg, 0, kStatusY);
    } else {
        g.setTextColor(TFT_DARKGREY, TFT_BLACK);
        g.drawString(";up .down  ENTER edit", 0, kStatusY);
    }

    g.setTextColor(TFT_DARKGREY, TFT_BLACK);
    g.drawString("SPC play n new DEL rm `back", 0, kHintY);
}

static void drawEditor(LovyanGFX &g)
{
    const std::string &txt = gBuf.text();

    char title[20];
    std::snprintf(title, sizeof(title), "%02u%s", static_cast<unsigned>(gCurrentId),
                  gUnsaved ? " *" : "");
    g.setTextColor(gPlayer.isPlaying() ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
    g.drawString(gPlayer.isPlaying() ? "PLAYING" : title, 0, kTitleY);
    drawRightAligned(g, gHeaderLine.c_str(), kTitleY, TFT_DARKGREY);

    const std::vector<texted::Line> lines = texted::wrapLines(txt, kCols);

    size_t curRow = 0, curCol = 0;
    texted::cursorRowCol(lines, gBuf.cursor(), curRow, curCol);

    // 正在响的音符在正文里的范围（用来反色高亮）
    size_t hiStart = 0, hiEnd = 0;
    bool hasHighlight = false;
    const int playIdx = gPlayer.currentIndex();
    if (gPlayingOwnScore && playIdx >= 0 && playIdx < static_cast<int>(gScore.notes.size())) {
        const jianpu::Note &n = gScore.notes[playIdx];
        if (n.srcPos >= gBodyOffset) {
            hiStart = n.srcPos - gBodyOffset;
            hiEnd = hiStart + n.srcLen;
            hasHighlight = true;
        }
    }

    // 播放时跟着高亮滚，不播时跟着光标滚
    size_t anchorRow = curRow;
    if (hasHighlight) {
        size_t r = 0, c = 0;
        texted::cursorRowCol(lines, hiStart, r, c);
        anchorRow = r;
    }
    const size_t first = (anchorRow >= kVisibleRows) ? anchorRow - (kVisibleRows - 1) : 0;

    for (size_t i = 0; i < kVisibleRows && first + i < lines.size(); ++i) {
        const texted::Line &ln = lines[first + i];
        const int y = kBodyY + static_cast<int>(i) * kCharH;

        g.setTextColor(TFT_CYAN, TFT_BLACK);
        g.drawString(txt.substr(ln.start, ln.len).c_str(), 0, y);

        if (!hasHighlight) continue;

        // 高亮范围和这一行的交集
        const size_t a = (hiStart > ln.start) ? hiStart : ln.start;
        const size_t b = (hiEnd < ln.start + ln.len) ? hiEnd : ln.start + ln.len;
        if (a >= b) continue;

        const int x = static_cast<int>(a - ln.start) * kCharW;
        const int w = static_cast<int>(b - a) * kCharW;
        g.fillRect(x, y, w, kCharH, TFT_GREEN);
        g.setTextColor(TFT_BLACK, TFT_GREEN);
        g.drawString(txt.substr(a, b - a).c_str(), x, y);
    }

    if (curRow >= first && curRow < first + kVisibleRows) {
        const int cy = kBodyY + static_cast<int>(curRow - first) * kCharH;
        g.fillRect(static_cast<int>(curCol) * kCharW, cy, 2, kCharH, TFT_WHITE);
    }

    char status[40];
    if (!gScore.error.ok) {
        // error.pos 是完整文本里的下标，屏幕上只显示正文，得减掉头部长度
        const unsigned at = (gScore.error.pos >= gBodyOffset)
                                ? static_cast<unsigned>(gScore.error.pos - gBodyOffset)
                                : 0;
        std::snprintf(status, sizeof(status), "at %u: %s", at, gScore.error.reason);
        g.setTextColor(TFT_RED, TFT_BLACK);
    } else {
        const float beats = gScore.totalBeats();
        const float secs = beats * 60.0f / static_cast<float>(gScore.header.bpm);
        std::snprintf(status, sizeof(status), "%u notes %.0f beats %.1fs",
                      static_cast<unsigned>(gScore.notes.size()), beats, secs);
        g.setTextColor(TFT_DARKGREY, TFT_BLACK);
    }
    g.drawString(status, 0, kStatusY);

    g.setTextColor(TFT_DARKGREY, TFT_BLACK);
    g.drawString(gPlayer.isPlaying() ? "ENTER stop" : "ENTER play  =setup  `back", 0, kHintY);
}

static void drawSettings(LovyanGFX &g)
{
    g.setTextColor(TFT_WHITE, TFT_BLACK);
    g.drawString("SETUP", 0, kTitleY);

    char id[8];
    std::snprintf(id, sizeof(id), "%02u", static_cast<unsigned>(gCurrentId));
    drawRightAligned(g, id, kTitleY, TFT_DARKGREY);

    char line[32];
    std::snprintf(line, sizeof(line), "key    1=%s", kKeyNames[gKeyIdx]);
    g.setTextColor(TFT_CYAN, TFT_BLACK);
    g.drawString(line, 8, kBodyY);

    std::snprintf(line, sizeof(line), "tempo  %d bpm", gBpm);
    g.drawString(line, 8, kBodyY + kCharH);

    std::snprintf(line, sizeof(line), "meter  %s", gTimeSig.c_str());
    g.setTextColor(TFT_DARKGREY, TFT_BLACK);
    g.drawString(line, 8, kBodyY + kCharH * 2);

    g.drawString("[ ] key   ;+ .- tempo", 0, kStatusY);
    g.drawString("ENTER done", 0, kHintY);
}

static void drawMenu(LovyanGFX &g)
{
    g.setTextColor(TFT_WHITE, TFT_BLACK);
    g.drawString("STACKM5", 0, kTitleY);

    g.setTextColor(TFT_CYAN, TFT_BLACK);
    g.drawString("1  JIANPU PLAYER", 8, kBodyY);
    g.drawString("2  VOCAB", 8, kBodyY + kCharH);
    g.drawString("3  TV REMOTE", 8, kBodyY + kCharH * 2);

    g.setTextColor(TFT_DARKGREY, TFT_BLACK);
    g.drawString("press 1-3", 0, kHintY);
}

static void draw()
{
    LovyanGFX &g = gCanvas ? static_cast<LovyanGFX &>(*gCanvas)
                           : static_cast<LovyanGFX &>(M5Cardputer.Display);

    if (!gCanvas) M5Cardputer.Display.startWrite();

    g.fillScreen(TFT_BLACK);
    g.setFont(&fonts::AsciiFont8x16);
    g.setTextDatum(top_left);

    switch (gPage) {
        case Page::Menu:
            drawMenu(g);
            break;
        case Page::Vocab:
            vocab_app::draw(g);
            break;
        case Page::Remote:
            remote_app::draw(g);
            break;
        case Page::Library:
            drawLibrary(g);
            break;
        case Page::Editor:
            drawEditor(g);
            break;
        case Page::Settings:
            drawSettings(g);
            break;
    }

    if (gCanvas) {
        gCanvas->pushSprite(0, 0);
    } else {
        M5Cardputer.Display.endWrite();
    }
}

// ── 按键 ────────────────────────────────────────────────────

static void handleMenuKeys(const Keyboard_Class::KeysState &st)
{
    for (const char c : st.word) {
        if (c == '1') {
            gPage = Page::Library;
            refreshEntries();
            gDirty = true;
        } else if (c == '2') {
            vocab_app::begin();
            gPage = Page::Vocab;
            gDirty = true;
        } else if (c == '3') {
            remote_app::begin();
            gPage = Page::Remote;
            gDirty = true;
        }
    }
}

static void handleVocabKeys(const Keyboard_Class::KeysState &st)
{
    // 背单词页只认三个键，⏎ 单独喂进去（它不出现在 word 里）
    if (st.enter) {
        vocab_app::handleKey('\n');
        gDirty = true;
    }
    for (const char c : st.word) {
        if (!vocab_app::handleKey(c)) {
            gPage = Page::Menu;
        }
        gDirty = true;
    }
}

static void handleRemoteKeys(const Keyboard_Class::KeysState &st)
{
    if (st.enter) {
        remote_app::handleEnter();
        gDirty = true;
    }
    for (const char c : st.word) {
        if (!remote_app::handleChar(c)) {
            gPage = Page::Menu;
        }
        gDirty = true;
    }
}

static void handleLibraryKeys(const Keyboard_Class::KeysState &st)
{
    if (st.del) {
        if (gEntries.empty()) return;
        if (gConfirmDelete) {
            library::remove(gEntries[gSel].id);
            gConfirmDelete = false;
            refreshEntries();
        } else {
            gConfirmDelete = true;
        }
        gDirty = true;
        return;
    }

    if (st.enter) {
        gConfirmDelete = false;
        if (!gEntries.empty()) openEditor(gEntries[gSel].id);
        return;
    }

    for (const char c : st.word) {
        gConfirmDelete = false;  // 按了别的键就取消删除确认

        if (c == '`') {
            gPlayer.stop();
            gPage = Page::Menu;
            gDirty = true;
            return;
        }
        if (c == ';') {
            if (gSel > 0) --gSel;
            gDirty = true;
        } else if (c == '.') {
            if (gSel + 1 < gEntries.size()) ++gSel;
            gDirty = true;
        } else if (c == ' ') {
            if (gPlayer.isPlaying()) {
                gPlayer.stop();
            } else if (!gEntries.empty()) {
                playById(gEntries[gSel].id);
            }
            gDirty = true;
        } else if (c == 'n') {
            const int id = library::createNew(std::string(kDefaultHeader) + "\n");
            if (id > 0) {
                refreshEntries();
                openEditor(static_cast<uint8_t>(id));
            }
            gDirty = true;
        } else {
            gDirty = true;  // 至少要重绘一次把确认提示擦掉
        }
    }
}

static void handleEditorKeys(const Keyboard_Class::KeysState &st)
{
    if (st.enter) {
        if (gPlayer.isPlaying()) {
            gPlayer.stop();
        } else if (gScore.error.ok && !gScore.notes.empty()) {
            gPlayingOwnScore = true;
            gPlayer.start(gScore);
        }
        gDirty = true;
    }

    if (st.del) {
        gBuf.backspace();
        touched();  // 删除不试听
    }

    // 空格既会置 st.space 也会出现在 word 里，所以只认 word，
    // 否则按一下空格会插进去两个。
    for (const char c : st.word) {
        if (c == '`') {
            gPlayer.stop();
            leaveEditor();
            return;
        }
        if (c == '=') {
            gPlayer.stop();
            gPage = Page::Settings;
            gDirty = true;
            return;
        }
        if (c == '[') {
            st.fn ? gBuf.moveHome() : gBuf.moveLeft();
            gDirty = true;
        } else if (c == ']') {
            st.fn ? gBuf.moveEnd() : gBuf.moveRight();
            gDirty = true;
        } else if (isJianpuChar(c) && gBuf.text().size() < kMaxChars) {
            gBuf.insert(c);
            touched();
            previewNoteBeforeCursor();
        }
    }
}

static void handleSettingsKeys(const Keyboard_Class::KeysState &st)
{
    if (st.enter) {
        stateToHeader();
        touched();
        gPage = Page::Editor;
        return;
    }

    for (const char c : st.word) {
        if (c == '`') {
            stateToHeader();
            touched();
            gPage = Page::Editor;
            return;
        }
        if (c == '[') {
            gKeyIdx = (gKeyIdx + kKeyCount - 1) % kKeyCount;
            previewTonic();
            gDirty = true;
        } else if (c == ']') {
            gKeyIdx = (gKeyIdx + 1) % kKeyCount;
            previewTonic();
            gDirty = true;
        } else if (c == ';') {
            if (gBpm < 300) gBpm += 5;
            gDirty = true;
        } else if (c == '.') {
            if (gBpm > 40) gBpm -= 5;
            gDirty = true;
        }
    }
}

static void handleKeys()
{
    if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) return;

    const Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();

    switch (gPage) {
        case Page::Menu:
            handleMenuKeys(st);
            break;
        case Page::Vocab:
            handleVocabKeys(st);
            break;
        case Page::Remote:
            handleRemoteKeys(st);
            break;
        case Page::Library:
            handleLibraryKeys(st);
            break;
        case Page::Editor:
            handleEditorKeys(st);
            break;
        case Page::Settings:
            handleSettingsKeys(st);
            break;
    }
}

// ───────────────────────────────────────────────────────────

void setup()
{
    auto cfg = M5.config();
    M5Cardputer.begin(cfg);

    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(120);
    M5Cardputer.Speaker.setVolume(180);

    // 第一次开机要格式化那 1.5MB 分区，会卡几秒。先告诉用户一声，
    // 免得看着黑屏以为死机了。
    M5Cardputer.Display.setFont(&fonts::AsciiFont8x16);
    M5Cardputer.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5Cardputer.Display.drawString("mounting storage...", 0, kBodyY);

    library::begin();
    seedExamples();
    refreshEntries();

    // 离屏画布消除闪烁。240x135x16bpp = 64.8KB，内存不够就退回直接画屏幕。
    gCanvas = new M5Canvas(&M5Cardputer.Display);
    gCanvas->setColorDepth(16);
    if (!gCanvas->createSprite(kScreenW, kScreenH)) {
        delete gCanvas;
        gCanvas = nullptr;
    }

    // 开机恢复：光条停在上次打开的那首上
    const int last = library::lastOpened();
    if (last > 0) {
        for (size_t i = 0; i < gEntries.size(); ++i) {
            if (gEntries[i].id == last) {
                gSel = i;
                break;
            }
        }
    }
}

void loop()
{
    M5Cardputer.update();
    gPlayer.update();
    handleKeys();

    // 自动存盘：改动后 1.5 秒无操作才写。Flash 有擦写寿命，
    // 每敲一个字符就写一次会很快磨坏它。
    // 只在真正会改谱子的两页上自动存盘。写成「不等于 Library」的话，
    // 每加一个新页面（菜单、背单词）都会意外落进这个条件里。
    const bool editing = (gPage == Page::Editor || gPage == Page::Settings);
    if (gUnsaved && editing && millis() - gLastEditMs > kAutosaveMs) {
        saveNow();
        gDirty = true;
    }

    // 遥控页的连接状态由 BLE 回调异步改变，不靠按键触发，所以定期重绘
    if (gPage == Page::Remote) {
        static uint32_t lastPoll = 0;
        if (millis() - lastPoll > 300) {
            lastPoll = millis();
            gDirty = true;
        }
    }

    if (gPlayer.isPlaying() != gWasPlaying) {
        gWasPlaying = gPlayer.isPlaying();
        gDirty = true;
    }

    // 播放时音符一变就重绘，让高亮跟着走
    if (gPlayer.currentIndex() != gLastPlayIdx) {
        gLastPlayIdx = gPlayer.currentIndex();
        gDirty = true;
    }

    if (gDirty) {
        gDirty = false;
        draw();
    }

    delay(5);
}
