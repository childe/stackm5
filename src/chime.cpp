#include "chime.h"

#include <M5Cardputer.h>

#include "timbre.h"

namespace {

constexpr float kHz[] = {1318.5f, 1760.0f};
constexpr int kNotes = sizeof(kHz) / sizeof(kHz[0]);
constexpr uint32_t kNoteMs = 120;

int gNote = -1;        // 正在响第几个音，-1 = 不响
uint32_t gNoteAt = 0;  // 这个音开始的时刻

void note(int i)
{
    const timbre::Voice &v = timbre::current();
    M5Cardputer.Speaker.tone(kHz[i], kNoteMs, -1, true, v.wave, v.len);
    gNote = i;
    gNoteAt = millis();
}

}  // namespace

void chime::play()
{
    note(0);
}

void chime::poll()
{
    if (gNote < 0 || millis() - gNoteAt < kNoteMs) return;
    if (gNote + 1 < kNotes) {
        note(gNote + 1);
    } else {
        gNote = -1;
    }
}
