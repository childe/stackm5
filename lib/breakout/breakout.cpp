#include "breakout.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPaddleW = 54.0f;
constexpr float kPaddleH = 7.0f;
constexpr float kBallRadius = 4.0f;
constexpr float kLaunchVx = 70.0f;
constexpr float kLaunchVy = -105.0f;
constexpr float kMaxStepSeconds = 1.0f / 120.0f;
constexpr int kBrickColumns = 8;
constexpr int kBrickRows = 4;
constexpr float kBrickW = 35.0f;
constexpr float kBrickH = 11.0f;
constexpr float kBrickGap = 3.0f;
constexpr float kBrickTop = 32.0f;

}  // namespace

namespace breakout {

Game::Game(int width, int height) : _width(width), _height(height)
{
    reset();
}

void Game::reset()
{
    _paddle = {(static_cast<float>(_width) - kPaddleW) / 2.0f, static_cast<float>(_height) - 18.0f,
               kPaddleW, kPaddleH};
    _bricks.clear();

    const float totalW = kBrickColumns * kBrickW + (kBrickColumns - 1) * kBrickGap;
    const float left = (static_cast<float>(_width) - totalW) / 2.0f;
    for (int row = 0; row < kBrickRows; ++row) {
        for (int col = 0; col < kBrickColumns; ++col) {
            _bricks.push_back({{left + col * (kBrickW + kBrickGap), kBrickTop + row * (kBrickH + kBrickGap),
                                kBrickW, kBrickH},
                               static_cast<uint8_t>(row)});
        }
    }

    _score = 0;
    _state = State::Ready;
    resetBallOnPaddle();
}

void Game::resetBallOnPaddle()
{
    _ball = {_paddle.x + _paddle.w / 2.0f, _paddle.y - kBallRadius - 1.0f, 0.0f, 0.0f, kBallRadius};
}

void Game::launch()
{
    if (_state != State::Ready) return;

    _ball.vx = kLaunchVx;
    _ball.vy = kLaunchVy;
    _state = State::Playing;
}

void Game::setTilt(float normalizedTilt)
{
    normalizedTilt = std::clamp(normalizedTilt, -1.0f, 1.0f);
    const float available = static_cast<float>(_width) - _paddle.w;
    _paddle.x = (normalizedTilt + 1.0f) * 0.5f * available;

    if (_state == State::Ready) resetBallOnPaddle();
}

bool Game::ballHitsRect(const Rect &rect) const
{
    const float nearestX = std::clamp(_ball.x, rect.x, rect.x + rect.w);
    const float nearestY = std::clamp(_ball.y, rect.y, rect.y + rect.h);
    const float dx = _ball.x - nearestX;
    const float dy = _ball.y - nearestY;
    return dx * dx + dy * dy <= _ball.radius * _ball.radius;
}

void Game::reflectFromBrick(size_t index, float previousY)
{
    const Rect brick = _bricks[index].rect;
    const bool hitTopOrBottom = previousY + _ball.radius <= brick.y ||
                                previousY - _ball.radius >= brick.y + brick.h;
    if (hitTopOrBottom) {
        _ball.vy = -_ball.vy;
    } else {
        _ball.vx = -_ball.vx;
    }

    _bricks.erase(_bricks.begin() + static_cast<std::ptrdiff_t>(index));
    ++_score;
    if (_bricks.empty()) _state = State::Won;
}

void Game::update(float deltaSeconds)
{
    if (_state != State::Playing || deltaSeconds <= 0.0f) return;

    // 大帧拆小步，避免球在低帧率下穿过挡板或砖块。
    while (deltaSeconds > 0.0f && _state == State::Playing) {
        const float step = std::min(deltaSeconds, kMaxStepSeconds);
        deltaSeconds -= step;

        const float previousY = _ball.y;
        _ball.x += _ball.vx * step;
        _ball.y += _ball.vy * step;

        if (_ball.x - _ball.radius < 0.0f) {
            _ball.x = _ball.radius;
            _ball.vx = std::abs(_ball.vx);
        } else if (_ball.x + _ball.radius > _width) {
            _ball.x = static_cast<float>(_width) - _ball.radius;
            _ball.vx = -std::abs(_ball.vx);
        }
        if (_ball.y - _ball.radius < 0.0f) {
            _ball.y = _ball.radius;
            _ball.vy = std::abs(_ball.vy);
        }

        if (_ball.vy > 0.0f && ballHitsRect(_paddle)) {
            _ball.y = _paddle.y - _ball.radius - 0.1f;
            const float offset = ((_ball.x - (_paddle.x + _paddle.w / 2.0f)) / (_paddle.w / 2.0f));
            _ball.vx = std::clamp(offset * 130.0f, -130.0f, 130.0f);
            _ball.vy = -std::abs(_ball.vy);
        }

        for (size_t i = 0; i < _bricks.size(); ++i) {
            if (ballHitsRect(_bricks[i].rect)) {
                reflectFromBrick(i, previousY);
                break;
            }
        }

        if (_ball.y - _ball.radius > _height) _state = State::Lost;
    }
}

}  // namespace breakout
