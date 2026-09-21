#include "player.h"

#include <M5Cardputer.h>
#include <vizmodel.h>

void Player::start(const jianpu::Score &score)
{
    // 无条件清 _paused：上一首暂停着被 stop 掉又立刻播下一首时，
    // 残留的 _paused 会让 update() 一进来就 return（全哑 + 冻屏）
    _paused = false;

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
    _paused = false;  // 同 start()：非法组合 (false,true) 绝不能留下
    _playing = false;
    _index = -1;
    M5Cardputer.Speaker.stop();
}

void Player::pause()
{
    if (!_playing || _paused) return;  // Stopped / Paused 下是 no-op

    _pausedElapsed = millis() - _startMs;
    M5Cardputer.Speaker.stop();
    _paused = true;
}

void Player::resume()
{
    if (!_playing || !_paused) return;  // 其余状态下是 no-op

    _startMs = vizmodel::resumeStartMs(millis(), _pausedElapsed);
    _paused = false;

    // 按**冻结的那一刻**重新定位音符并同步 _index，不能沿用 update() 留下的
    // 旧下标：pause() 记的 elapsed 比上一次 update() 更晚，可能已经跨进下一个
    // 音，而 _index 还停在旧音上 —— 拿旧下标算剩余时长必然得 0（旧音的 hold
    // 早过完了），当前这个音于是要空等一整帧，再由 update() 用整段 holdMs
    // 重新触发，收尾也因此偏晚。
    //
    // 同步之后 update() 下一轮算出的 idx 仍等于 _index，会在「还在同一个音里」
    // 那一支提前 return，不会重复触发。
    const vizmodel::ResumePoint at = vizmodel::resumePointAt(_timeline, _pausedElapsed);
    _index = at.index;

    // 冻结点已过曲末：不补音，交给下一轮 update() 去 stop()
    if (at.index < 0 || static_cast<size_t>(at.index) >= _score.notes.size()) return;

    // 补音：不补的话恢复后半个音是哑的，听起来像丢一拍
    const float freq = jianpu::noteToFreq(_score.notes[at.index], _score.header);
    if (at.restMs > 0 && freq > 0.0f) M5Cardputer.Speaker.tone(freq, at.restMs);
}

int Player::currentIndex() const
{
    if (!_playing) return -1;
    if (_paused) return jianpu::indexAt(_timeline, _pausedElapsed);  // 冻结，多次调用不漂移
    return _index;
}

uint32_t Player::elapsedMs() const
{
    if (!_playing) return 0;
    if (_paused) return _pausedElapsed;
    return millis() - _startMs;
}

PlaybackFrame Player::frame() const
{
    PlaybackFrame f;
    f.playing = _playing;
    f.paused = _paused;
    f.totalMs = _timeline.totalMs;
    if (!_playing) return f;

    // millis() 只取一次（elapsedMs() 内部），index 从同一个 elapsed 算出来
    f.elapsedMs = elapsedMs();
    f.index = jianpu::indexAt(_timeline, f.elapsedMs);

    if (f.index >= 0 && static_cast<size_t>(f.index) < _score.notes.size()) {
        f.onsetMs = _timeline.onsetMs[f.index];
        f.holdMs = _timeline.holdMs[f.index];
        f.freq = jianpu::noteToFreq(_score.notes[f.index], _score.header);
    }

    return f;
}

void Player::update()
{
    if (!_playing || _paused) return;  // 暂停期间不推进 _index、不碰喇叭

    // millis() 只读一次，下标和剩余时长都出自这一个 elapsed。
    // 和 resume() 共用同一条推导，两边不会各自算出不一致的「当前音」
    const vizmodel::ResumePoint at = vizmodel::resumePointAt(_timeline, millis() - _startMs);

    if (at.index < 0) {  // 播完了
        stop();          // 于是 _paused 也被清掉
        return;
    }
    if (at.index == _index) return;  // 还在同一个音里，什么都不用做

    _index = at.index;

    const jianpu::Note &n = _score.notes[at.index];
    const float freq = jianpu::noteToFreq(n, _score.header);

    if (freq > 0.0f) {
        // holdMs 已经是时长的 85%，留出的间隙让连续相同的音能分开听。
        // 这里刻意用整段 holdMs 而不是 at.restMs：新音刚起，两者本来就几乎
        // 相等，而极端卡顿（一帧跨过整个音）下 restMs 会是 0，会把音整个吞掉
        M5Cardputer.Speaker.tone(freq, _timeline.holdMs[at.index]);
    } else {
        M5Cardputer.Speaker.stop();  // 休止符
    }
}
