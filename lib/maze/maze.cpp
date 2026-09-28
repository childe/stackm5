#include "maze.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kBallRadius = 5.0f;
constexpr float kAcceleration = 310.0f;
constexpr float kDampingPerSecond = 0.25f;
constexpr float kMaxVelocity = 115.0f;
constexpr float kMaxStepSeconds = 1.0f / 120.0f;
constexpr float kWall = 5.0f;

}  // namespace

namespace maze {

Game::Game(int width, int height) : _width(width), _height(height)
{
    reset();
}

void Game::reset()
{
    _walls = {
        {0, 0, static_cast<float>(_width), kWall},
        {0, static_cast<float>(_height) - kWall, static_cast<float>(_width), kWall},
        {0, 0, kWall, static_cast<float>(_height)},
        {static_cast<float>(_width) - kWall, 0, kWall, static_cast<float>(_height)},
        {45, 35, 190, 6},
        {45, 35, 6, 75},
        {105, 80, 170, 6},
        {270, 80, 6, 85},
        {75, 130, 200, 6},
        {75, 130, 6, 70},
        {75, 195, 170, 6},
        {240, 155, 6, 46},
        {145, 155, 100, 6},
    };
    _goal = {278, 203, 29, 27};
    _traps = {
        {54, 49, 42, 22},
        {181, 91, 38, 21},
        {108, 142, 46, 22},
        {187, 166, 35, 20},
    };
    resetBall();
    _tiltX = 0.0f;
    _tiltY = 0.0f;
    _state = State::Ready;
    _trapHits = 0;
    _trapWarningSeconds = 0.0f;
}

void Game::start()
{
    if (_state == State::Ready) _state = State::Playing;
}

void Game::setTilt(float x, float y)
{
    _tiltX = std::clamp(x, -1.0f, 1.0f);
    _tiltY = std::clamp(y, -1.0f, 1.0f);
}

bool Game::ballHits(const Rect &rect) const
{
    const float nearestX = std::clamp(_ball.x, rect.x, rect.x + rect.w);
    const float nearestY = std::clamp(_ball.y, rect.y, rect.y + rect.h);
    const float dx = _ball.x - nearestX;
    const float dy = _ball.y - nearestY;
    return dx * dx + dy * dy < _ball.radius * _ball.radius;
}

void Game::resetBall()
{
    _ball = {23, 20, 0, 0, kBallRadius};
}

void Game::triggerTrap()
{
    ++_trapHits;
    _trapWarningSeconds = 0.7f;
    resetBall();
}

bool Game::ballInside(const Rect &rect) const
{
    return _ball.x - _ball.radius >= rect.x && _ball.x + _ball.radius <= rect.x + rect.w &&
           _ball.y - _ball.radius >= rect.y && _ball.y + _ball.radius <= rect.y + rect.h;
}

void Game::resolveX(float previousX)
{
    for (const Rect &wall : _walls) {
        if (!ballHits(wall)) continue;

        if (previousX + _ball.radius <= wall.x) {
            _ball.x = wall.x - _ball.radius;
        } else if (previousX - _ball.radius >= wall.x + wall.w) {
            _ball.x = wall.x + wall.w + _ball.radius;
        }
        _ball.vx = 0.0f;
    }
}

void Game::resolveY(float previousY)
{
    for (const Rect &wall : _walls) {
        if (!ballHits(wall)) continue;

        if (previousY + _ball.radius <= wall.y) {
            _ball.y = wall.y - _ball.radius;
        } else if (previousY - _ball.radius >= wall.y + wall.h) {
            _ball.y = wall.y + wall.h + _ball.radius;
        }
        _ball.vy = 0.0f;
    }
}

void Game::update(float deltaSeconds)
{
    if (_state != State::Playing || deltaSeconds <= 0.0f) return;

    _trapWarningSeconds = std::max(0.0f, _trapWarningSeconds - deltaSeconds);

    while (deltaSeconds > 0.0f && _state == State::Playing) {
        const float step = std::min(deltaSeconds, kMaxStepSeconds);
        deltaSeconds -= step;

        _ball.vx = std::clamp((_ball.vx + _tiltX * kAcceleration * step) * (1.0f - kDampingPerSecond * step),
                              -kMaxVelocity, kMaxVelocity);
        _ball.vy = std::clamp((_ball.vy + _tiltY * kAcceleration * step) * (1.0f - kDampingPerSecond * step),
                              -kMaxVelocity, kMaxVelocity);

        const float previousX = _ball.x;
        _ball.x += _ball.vx * step;
        resolveX(previousX);

        const float previousY = _ball.y;
        _ball.y += _ball.vy * step;
        resolveY(previousY);

        for (const Rect &trap : _traps) {
            if (ballHits(trap)) {
                triggerTrap();
                break;
            }
        }
        if (_trapWarningSeconds > 0.0f) break;

        if (ballInside(_goal)) _state = State::Won;
    }
}

}  // namespace maze
