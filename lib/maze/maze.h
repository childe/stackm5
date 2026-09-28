// 倾斜走迷宫的纯物理状态机。屏幕/IMU 层只负责给出 [-1,1] 的倾斜量与绘制。
#pragma once

#include <cstdint>
#include <vector>

namespace maze {

struct Rect {
    float x;
    float y;
    float w;
    float h;
};

struct Ball {
    float x;
    float y;
    float vx;
    float vy;
    float radius;
};

enum class State { Ready, Playing, Won };

class Game {
   public:
    Game(int width, int height);

    void reset();
    void start();
    void setTilt(float x, float y);
    void update(float deltaSeconds);

    State state() const { return _state; }
    const Ball &ball() const { return _ball; }
    const std::vector<Rect> &walls() const { return _walls; }
    const std::vector<Rect> &traps() const { return _traps; }
    const Rect &goal() const { return _goal; }
    uint32_t trapHits() const { return _trapHits; }
    bool isTrapWarning() const { return _trapWarningSeconds > 0.0f; }

   private:
    bool ballHits(const Rect &rect) const;
    bool ballInside(const Rect &rect) const;
    void resetBall();
    void triggerTrap();
    void resolveX(float previousX);
    void resolveY(float previousY);

    int _width;
    int _height;
    Ball _ball{};
    Rect _goal{};
    std::vector<Rect> _walls;
    std::vector<Rect> _traps;
    float _tiltX = 0.0f;
    float _tiltY = 0.0f;
    State _state = State::Ready;
    uint32_t _trapHits = 0;
    float _trapWarningSeconds = 0.0f;
};

}  // namespace maze
