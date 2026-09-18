#include "remotemap.h"

namespace remotemap {

/*
 * 按键分配的理由：
 *   ; . , /   方向 —— Cardputer 社区惯例，肌肉记忆
 *   = -       音量 —— 键面上就是加减号
 *   m h b p   静音/主页/返回/电源 —— 首字母
 *   `         回菜单 —— 物理位置在左上角，像 Esc；和另外两个 app 一致
 *
 * ⏎ 是独立的键盘标志位、不出现在 word 里，所以不在这张表中（见 fromEnter）。
 */
const Binding kBindings[] = {
    {';', Action::Up},
    {'.', Action::Down},
    {',', Action::Left},
    {'/', Action::Right},
    {'=', Action::VolUp},
    {'-', Action::VolDown},
    {'m', Action::Mute},
    {'h', Action::Home},
    {'b', Action::Back},
    {'p', Action::Power},
    {'`', Action::ExitApp},
};
const size_t kBindingCount = sizeof(kBindings) / sizeof(kBindings[0]);

Action fromChar(char c)
{
    if (c == '\0') return Action::None;
    for (size_t i = 0; i < kBindingCount; ++i) {
        if (kBindings[i].key == c) return kBindings[i].action;
    }
    return Action::None;
}

Action fromEnter()
{
    return Action::Ok;
}

const char *label(Action a)
{
    switch (a) {
        case Action::Up: return "up";
        case Action::Down: return "down";
        case Action::Left: return "left";
        case Action::Right: return "right";
        case Action::Ok: return "ok";
        case Action::Back: return "back";
        case Action::Home: return "home";
        case Action::Mute: return "mute";
        case Action::VolUp: return "vol+";
        case Action::VolDown: return "vol-";
        case Action::Power: return "power";
        case Action::ExitApp: return "exit";
        case Action::None: break;
    }
    return "-";
}

}  // namespace remotemap
