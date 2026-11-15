// 低电量提醒：每 5 秒读一次电压交给 lib/lowbat 判断，变差时响一声并在当前页上
// 盖一个红框，按任意键关掉。标题行的电池图标 Low 常红、Critical 闪红。
//
// 判断规则（阈值、平均、只提醒一次）见 lib/lowbat/lowbat.h
#pragma once

#include <M5GFX.h>

namespace lowbat_alert {

// loop 每圈调一次。需要重绘（新提醒弹出）时返回 true
bool tick();

// 标题行电池图标该不该画红
bool iconRed();

// 红框在就关掉它并返回 true，这次按键不再交给页面
bool dismiss();

// 在页面画完之后、推屏之前调；红框不在时什么都不画
void drawOverlay(LovyanGFX &g);

}  // namespace lowbat_alert
