// 全局配色方案，所有页面共用。
//
// 为什么做成可选：纯白字 + 纯黑底 + 满饱和青 / 橙在这块小屏上晃眼，但哪套舒服因人因光线而异。
// 七套：classic 是原来的配色（默认，保持之前的样子），其余是常见的暗色终端配色降饱和版，
// amber 是单色相，夜里用。颜色都是 RGB565。
//
// 页面按「角色」取色，不再写死 TFT_*。原来的灰阶渐变（骰子、噪音计、背单词）用 level()，
// 从底色过渡到正文色；classic 下和原来的 color565(v, v, v) 逐位相同。
// 低电量红框不跟主题走 —— 它就是要扎眼。可视化页的四种配色是它自己的，也不跟主题走。
//
// 断电保存，1.5 秒无变化写一次 NVS（命名空间 cfg，键 theme）—— 做法同 volume。
#pragma once

#include <cstdint>

namespace theme {

struct Palette {
    const char *name;  // CONFIG 行里显示，ASCII，≤ 9 字符
    uint16_t bg;       // 底色（含顶栏）                         classic: 黑
    uint16_t fg;       // 最亮的字：标题、正文、光标              classic: TFT_WHITE
    uint16_t body;     // 次一级的字：未选中的行、C / X 标记       classic: TFT_LIGHTGREY
    uint16_t dim;      // 暗字：按键提示、目录、顶栏 info          classic: TFT_DARKGREY
    uint16_t faint;    // 更暗：蓝牙未启动图标                    classic: 0x4208
    uint16_t sel;      // 列表选中行底                            classic: 深蓝灰
    uint16_t accent;   // 选中项、运行中、强调                    classic: TFT_CYAN
    uint16_t warn;     // 该你了                                  classic: TFT_ORANGE
    uint16_t ok;       // 已连接、正在播放                        classic: TFT_GREEN
    uint16_t bad;      // 出错、静音叉、低电量                    classic: TFT_RED
    uint16_t flash;    // 提醒 / 报警闪屏底                       classic: 暗红
    uint8_t tint;      // 菜单图标的彩色往 fg 拉多少（0 = 原色，255 = 全成 fg）
};

// 从 NVS 读（没有或越界则 classic）。setup() 里调一次。
void begin();

// loop 每圈调一次：改动后静默 1.5 秒写一次 NVS。
void poll();

const Palette &current();

// 亮度级 0..255：0 = 底色，255 = 正文色，中间线性过渡
uint16_t level(int v);

// 循环切换，到头回到另一端
void next();
void prev();

}  // namespace theme
