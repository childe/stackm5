// 非阻塞播放器。
//
// 烟雾测试用的是 delay()，播放那十几秒整个设备锁死：按键无响应、不能中途停。
// 这里改成状态机：每次 loop() 调一次 update()，到点了才推进到下一个音，
// 主循环始终空着能读键盘。
//
// 「此刻该响第几个音」的计算在 lib/jianpu 的 Timeline 里（纯逻辑、有单元测试），
// 这个类只负责把它接到喇叭上。
#pragma once

#include <jianpu.h>

class Player {
public:
    void start(const jianpu::Score &score);
    void stop();
    void update();  // 每次 loop() 调一次

    bool isPlaying() const
    {
        return _playing;
    }

    // 当前正在响的音符下标，-1 = 没在播（UI 高亮用）
    int currentIndex() const
    {
        return _playing ? _index : -1;
    }

    uint32_t elapsedMs() const;

    uint32_t totalMs() const
    {
        return _timeline.totalMs;
    }

private:
    jianpu::Score _score;
    jianpu::Timeline _timeline;
    bool _playing = false;
    uint32_t _startMs = 0;
    int _index = -1;  // 已经触发过发声的音符
};
