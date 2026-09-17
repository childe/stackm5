#include "jianpu.h"

#include <cmath>
#include <cstring>

namespace jianpu {

namespace {

// 音名 → 距 C 的半音数。C=0 D=2 E=4 F=5 G=7 A=9 B=11，再按 # / b 调整。
bool keyNameToSemitone(const char *p, size_t len, int8_t &out)
{
    static const int8_t base[7] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G

    if (len == 0 || len > 2) return false;
    if (p[0] < 'A' || p[0] > 'G') return false;

    int semi = base[p[0] - 'A'];
    if (len == 2) {
        if (p[1] == '#') {
            semi += 1;
        } else if (p[1] == 'b') {
            semi -= 1;
        } else {
            return false;
        }
    }

    out = static_cast<int8_t>((semi + 12) % 12);
    return true;
}

// 解析头部行，形如 "1=C 4/4 120"。拍号解析但忽略（对发声没影响）。
// base 是这一行在整段文本里的起始下标，用来把错误位置换算成绝对下标。
bool parseHeaderLine(const char *p, size_t len, size_t base, Header &h, ParseError &err)
{
    size_t i = 0;
    while (i < len) {
        while (i < len && (p[i] == ' ' || p[i] == '\t')) ++i;

        const size_t start = i;
        while (i < len && p[i] != ' ' && p[i] != '\t') ++i;
        const size_t tokLen = i - start;
        if (tokLen == 0) continue;

        const char *tok = p + start;

        if (tokLen > 2 && tok[0] == '1' && tok[1] == '=') {
            if (!keyNameToSemitone(tok + 2, tokLen - 2, h.keyRoot)) {
                err.ok = false;
                err.pos = static_cast<uint16_t>(base + start + 2);
                err.reason = "bad key name";
                return false;
            }
            continue;
        }
        if (std::memchr(tok, '/', tokLen) != nullptr) {
            continue;  // 拍号，忽略
        }

        int value = 0;
        bool allDigits = true;
        for (size_t k = 0; k < tokLen; ++k) {
            if (tok[k] < '0' || tok[k] > '9') {
                allDigits = false;
                break;
            }
            value = value * 10 + (tok[k] - '0');
        }
        if (allDigits && value > 0) h.bpm = value;
    }

    return true;
}

// 如果文本以 "1=" 开头，第一行就是头部行。返回音符正文的起始下标。
size_t takeHeader(const char *text, size_t len, Header &h, ParseError &err)
{
    size_t p = 0;
    while (p < len && (text[p] == ' ' || text[p] == '\t')) ++p;

    if (p + 1 >= len || text[p] != '1' || text[p + 1] != '=') return 0;

    size_t eol = p;
    while (eol < len && text[eol] != '\n') ++eol;

    parseHeaderLine(text + p, eol - p, p, h, err);
    return (eol < len) ? eol + 1 : len;
}

void fail(Score &s, size_t pos, const char *reason)
{
    s.error.ok = false;
    s.error.pos = static_cast<uint16_t>(pos);
    s.error.reason = reason;
}

}  // namespace

float Score::totalBeats() const
{
    float sum = 0.0f;
    for (const Note &n : notes) sum += n.beats;
    return sum;
}

Score parse(const char *text, size_t len)
{
    Score s;

    const size_t bodyStart = takeHeader(text, len, s.header, s.error);
    if (!s.error.ok) return s;

    // 升降号写在数字前面，得先攒着；同时记下它的位置，
    // 这样音符的 srcPos 能从 '#' 而不是从数字算起。
    int8_t pendingAccidental = 0;
    size_t pendingAccidentalPos = 0;

    for (size_t i = bodyStart; i < len; ++i) {
        const char c = text[i];

        // 分隔符：空格 / tab / 换行，以及小节线（纯排版，对发声无影响）
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '|') continue;

        if (c == '#' || c == 'b') {
            // 必须紧贴一个音级数字，否则 "3 # 5" 会悄悄把 5 升半音
            const bool nextIsStep = (i + 1 < len) && text[i + 1] >= '0' && text[i + 1] <= '7';
            if (!nextIsStep) {
                fail(s, i, "# and b must be followed by 1-7");
                return s;
            }
            pendingAccidental = (c == '#') ? 1 : -1;
            pendingAccidentalPos = i;
            continue;
        }

        // 增时线是独立 token：不产生新音符，而是把前一个延长一拍，
        // 并把它的源码范围一路扩到这条增时线为止（高亮要整块）
        if (c == '-') {
            if (s.notes.empty()) {
                fail(s, i, "dash needs a note before it");
                return s;
            }
            Note &prev = s.notes.back();
            prev.beats += 1.0f;
            prev.srcLen = static_cast<uint16_t>(i - prev.srcPos + 1);
            continue;
        }

        if (c == '8' || c == '9') {
            fail(s, i, "only 1-7 (0 = rest)");
            return s;
        }

        if (c < '0' || c > '7') {
            fail(s, i, "unknown character");
            return s;
        }

        Note n;
        n.step = static_cast<uint8_t>(c - '0');
        n.accidental = pendingAccidental;
        n.beats = 1.0f;
        n.srcPos = static_cast<uint16_t>(pendingAccidental != 0 ? pendingAccidentalPos : i);
        pendingAccidental = 0;

        // 把紧跟在数字后面的修饰符都吃掉
        size_t j = i + 1;
        for (; j < len; ++j) {
            if (text[j] == '\'') {
                ++n.octave;
            } else if (text[j] == ',') {
                --n.octave;
            } else if (text[j] == '/') {
                n.beats *= 0.5f;  // 减时线：每条减半
            } else if (text[j] == '.') {
                n.beats *= 1.5f;  // 附点：×1.5
            } else {
                break;
            }
        }

        n.srcLen = static_cast<uint16_t>(j - n.srcPos);
        i = j - 1;  // 外层 for 的 ++i 会补回来

        s.notes.push_back(n);
    }

    return s;
}

size_t headerPrefixLen(const char *text, size_t len)
{
    Header ignoredHeader;
    ParseError ignoredError;
    return takeHeader(text, len, ignoredHeader, ignoredError);
}

Timeline buildTimeline(const Score &s)
{
    Timeline t;
    const int bpm = (s.header.bpm > 0) ? s.header.bpm : 120;
    const float msPerBeat = 60000.0f / static_cast<float>(bpm);

    t.onsetMs.reserve(s.notes.size());
    t.holdMs.reserve(s.notes.size());

    float acc = 0.0f;
    for (const Note &n : s.notes) {
        const float dur = n.beats * msPerBeat;
        t.onsetMs.push_back(static_cast<uint32_t>(acc + 0.5f));
        // 只响 85% 的时长，留 15% 静音间隙。
        // 否则连续相同的音（如 "3 3 3"）会粘成一个长音 —— 烟雾测试里听出来的。
        t.holdMs.push_back(static_cast<uint32_t>(dur * 0.85f + 0.5f));
        acc += dur;
    }

    t.totalMs = static_cast<uint32_t>(acc + 0.5f);
    return t;
}

int indexAt(const Timeline &t, uint32_t elapsedMs)
{
    if (elapsedMs >= t.totalMs) return -1;  // 播完了

    // onsetMs 是递增的，从后往前找第一个起始时刻 <= elapsedMs
    for (size_t i = t.onsetMs.size(); i > 0; --i) {
        if (elapsedMs >= t.onsetMs[i - 1]) return static_cast<int>(i - 1);
    }
    return -1;
}

float noteToFreq(const Note &n, const Header &h)
{
    // 大调音阶各级距主音的半音数。3→4 只差 1 个半音（钢琴上 E-F 之间没黑键），
    // 7→高音1 同理。下标 1..7 有效。
    static const int semitone[8] = {0, 0, 2, 4, 5, 7, 9, 11};

    if (n.step < 1 || n.step > 7) return 0.0f;  // 休止符不发声

    const int semi = semitone[n.step] + n.accidental + 12 * n.octave + h.keyRoot;
    return 261.626f * std::pow(2.0f, semi / 12.0f);
}

}  // namespace jianpu
