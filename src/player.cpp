#include "player.h"

#include <M5Cardputer.h>

void Player::start(const jianpu::Score &score)
{
    _score = score;
    _timeline = jianpu::buildTimeline(_score);

    if (_score.notes.empty() || _timeline.totalMs == 0) {
        _playing = false;
        return;
    }

    _startMs = millis();
    _index = -1;
    _playing = true;

    update();  // 立刻触发第一个音，不等下一轮 loop
}

void Player::stop()
{
    _playing = false;
    _index = -1;
    M5Cardputer.Speaker.stop();
}

uint32_t Player::elapsedMs() const
{
    return _playing ? (millis() - _startMs) : 0;
}

void Player::update()
{
    if (!_playing) return;

    const int idx = jianpu::indexAt(_timeline, millis() - _startMs);

    if (idx < 0) {  // 播完了
        stop();
        return;
    }
    if (idx == _index) return;  // 还在同一个音里，什么都不用做

    _index = idx;

    const jianpu::Note &n = _score.notes[idx];
    const float freq = jianpu::noteToFreq(n, _score.header);

    if (freq > 0.0f) {
        // holdMs 已经是时长的 85%，留出的间隙让连续相同的音能分开听
        M5Cardputer.Speaker.tone(freq, _timeline.holdMs[idx]);
    } else {
        M5Cardputer.Speaker.stop();  // 休止符
    }
}
