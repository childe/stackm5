// 重力滚球打砖块的纯物理状态机。坐标单位是像素；设备层只提供倾斜输入和绘制。
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace breakout {

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

enum class State { Ready, Playing, Won, Lost };

struct Brick {
    Rect rect;
    uint8_t row;
};

class Game {
   public:
    Game(int width, int height);

    void reset();
    void launch();
    void setTilt(float normalizedTilt);
    void update(float deltaSeconds);

    State state() const { return _state; }
    const Ball &ball() const { return _ball; }
    const Rect &paddle() const { return _paddle; }
    const std::vector<Brick> &bricks() const { return _bricks; }
    uint32_t score() const { return _score; }

   private:
    bool ballHitsRect(const Rect &rect) const;
    void resetBallOnPaddle();
    void reflectFromBrick(size_t index, float previousY);

    int _width;
    int _height;
    Ball _ball{};
    Rect _paddle{};
    std::vector<Brick> _bricks;
    State _state = State::Ready;
    uint32_t _score = 0;
};

}  // namespace breakout
