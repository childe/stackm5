// 遥控器的按键映射。纯逻辑，零硬件依赖 —— 放这里是为了能用单元测试守住按键冲突。
//
// 这个项目已经被按键冲突坑过一次：简谱 app 里 , . / 是低音点/附点/减时线，
// 撞了 Cardputer 社区惯例的方向键。映射表一旦有两个键指向同一个动作，
// 或者某个动作没有键，编译能过、测试能拦。
#pragma once

#include <cstddef>

namespace remotemap {

enum class Action {
    None,
    Up,
    Down,
    Left,
    Right,
    Ok,
    Back,
    Home,
    Mute,
    VolUp,
    VolDown,
    Power,
    ExitApp,  // 回菜单页，不发给电视
};

// 普通可打印键 → 动作。未映射返回 None。
Action fromChar(char c);

// ⏎ 是独立的键盘标志位，不出现在 word 里，所以单独一个入口
Action fromEnter();

// 动作 → 屏幕上显示的短名。None 返回 "-"
const char *label(Action a);

// 给测试用：遍历全部映射
struct Binding {
    char key;
    Action action;
};
extern const Binding kBindings[];
extern const size_t kBindingCount;

}  // namespace remotemap
