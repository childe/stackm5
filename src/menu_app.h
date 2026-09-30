// 首页：横向轮播菜单，一屏 3 个「图标 + 标签」，中间的是当前项。
//   , /        左转 / 右转（转一格滑动 160ms）
//   空格 或 ⏎   进入当前项
//   1-6        直接进入（顺序同轮播）
//
// 轮播的取模、缓动、格位几何都在 lib/carousel，这里只管 item 表和画图标。
// 菜单项的唯一来源是 menu_app.cpp 里的 kItems 表 —— DIAG 永远放最后。
#pragma once

#include <M5GFX.h>

namespace menu_app {

enum class App { None, Music, Vocab, Remote, Dice, Noise, Diag };

void draw(LovyanGFX &g);

// 只在菜单页调用。动画进行中约 16ms 返回一次 true（要求重绘），
// 动画刚停稳的那一次也返回 true，保证最后一帧画在 offset = 0 上
bool tick();

// 和其他 app 一样只收 char：⏎ 由调用方翻译成 '\n'。
// 返回要进入的 app；App::None 表示留在菜单页
App handleKey(char c);

}  // namespace menu_app
