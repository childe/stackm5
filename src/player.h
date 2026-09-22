// 非阻塞播放器。
//
// 烟雾测试用的是 delay()，播放那十几秒整个设备锁死：按键无响应、不能中途停。
// 这里改成状态机：每次 loop() 调一次 update()，到点了才推进到下一个音，
// 主循环始终空着能读键盘。
//
// 「此刻该响第几个音」的计算在 lib/music 的 Timeline 里（纯逻辑、有单元测试），
// 暂停 / 恢复的时间算术在 lib/vizmodel 里（同样纯逻辑、有单元测试），
// 这个类只负责把它们接到喇叭上。
//
// 播放状态由 (_playing, _paused) 表示，合法状态只有三个：
//   Stopped (false,false) 没挂曲子
//   Playing (true, false) 正常走时间轴
//   Paused  (true, true ) 挂着曲子但时间冻结
// (false,true) 是非法组合 —— start() 和 stop() 无条件清 _paused 来保证它
// 永不出现。漏了的话「暂停 → 停止 → 重新播另一首」会带着残留的 _paused
// 进新播放，update() 一进来就 return：喇叭全哑、画面冻在第一帧，而
// isPlaying() 还是 true，看起来像死机。
#pragma once

#include <music.h>

// 一次取齐的只读播放快照。可视化页的唯一数据源 —— 分别调 elapsedMs() /
// currentIndex() 会跨 millis() 边界拿到不自洽的组合（index 已经是下一个音、
// elapsed 还是上一个音的）。
struct PlaybackFrame {
    bool playing = false;
    bool paused = false;
    uint32_t elapsedMs = 0;  // paused 时为冻结值
    uint32_t totalMs = 0;
    int index = -1;        // -1 = 没在播
    uint32_t onsetMs = 0;  // index >= 0 时有效
    uint32_t holdMs = 0;
    float freq = 0.0f;  // <= 0 表示休止符
};

class Player {
public:
    void start(const music::Score &score);
    void stop();
    void pause();   // 只在 Playing 下生效
    void resume();  // 只在 Paused 下生效
    void update();  // 每次 loop() 调一次

    // 暂停时仍为 true：曲子还挂着，主循环不能误判成放完
    bool isPlaying() const
    {
        return _playing;
    }

    bool isPaused() const
    {
        return _paused;
    }

    // 当前正在响的音符下标，-1 = 没在播（UI 高亮用）
    int currentIndex() const;

    uint32_t elapsedMs() const;

    uint32_t totalMs() const
    {
        return _timeline.totalMs;
    }

    PlaybackFrame frame() const;

    // 谱面按值持有，生命周期覆盖整个播放过程，所以可以安全地暴露成只读。
    // 可视化页要谱面一律走这两个 —— 它不许自己再存一份、也不许自己维护
    // 第二条时间轴。
    const music::Score &score() const
    {
        return _score;
    }

    const music::Timeline &timeline() const
    {
        return _timeline;
    }

private:
    music::Score _score;
    music::Timeline _timeline;
    bool _playing = false;
    bool _paused = false;
    uint32_t _startMs = 0;
    uint32_t _pausedElapsed = 0;  // 暂停那一刻的 elapsed，冻结画面用
    int _index = -1;              // 已经触发过发声的音符
};
