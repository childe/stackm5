// 背单词页。
//
// 状态全部封在 vocab_app.cpp 里，不往 main.cpp 塞全局变量 —— main.cpp 只需要
// 知道「开始 / 画一帧 / 喂按键」这三件事。
#pragma once

#include <M5GFX.h>

namespace vocab_app {

// 进入这一页时调用：解析内置词表、抽第一个词
void begin();

void draw(LovyanGFX &g);

// 返回 false 表示用户按了 ` 要回菜单页
bool handleKey(char c);

}  // namespace vocab_app
