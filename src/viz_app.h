// 全屏可视化页。
//
// 曲库页按 SPC 播放成功就切到这一页，整屏画效果：
//   SPC 暂停 / 恢复   . 换风格   , 换配色   ` 停止并回列表
//
// 状态全封在 viz_app.cpp 里（和 vocab_app / remote_app 一样），main.cpp 只
// 需要知道「开始 / 画一帧 / 喂按键」这三件事。
//
// 不收 Score 参数：playById 里的 music::Score 是局部变量，start() 之后就
// 出作用域了。要谱面一律走 player.score() / player.timeline()（Player 按值
// 持有），绝不持有指向调用方局部量的引用或指针。
#pragma once

#include <M5GFX.h>

#include <cstdint>

#include "player.h"

namespace viz_app {

// 进入这一页时调用。title 会被拷进内部固定缓冲 —— 调用方给的是
// gEntries[gSel].preview.c_str()，而 refreshEntries() 会整个重建 gEntries，
// std::string 的缓冲连带失效，存下这个指针就是悬垂。
// id 只用来显示曲号。
void begin(const char *title, uint8_t id, const Player &player);

void draw(LovyanGFX &g, const Player &player);

// 返回 false 表示要回曲库页（停止播放和切页由 main.cpp 做）
bool handleKey(char c, Player &player);

}  // namespace viz_app
