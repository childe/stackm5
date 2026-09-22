// 音色（单周期波表）。
//
// M5Unified 的 tone() 有个吃自定义波形的重载：
//   tone(freq, duration, channel, stop_current_sound, raw_data, array_len)
// 它把采样率设成 freq * array_len，也就是把给定的「一个周期」按音高循环播放。
// 换音色因此只是换一个几十字节的数组，不需要流式合成。
//
// 默认音色（M5Unified 的 _default_tone_wav）是 16 点纯正弦，恒定音量 ——
// 这正是它听起来像测试音的原因。波表只改频谱；包络（起音/衰减）是另一件事，
// 目前还没做，所以四种音色都还是「一直响到时值结束」。
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace timbre {

struct Voice {
    const char *name;
    const uint8_t *wave;  // 单周期波表，128 为零点
    size_t len;           // 采样点数
};

// 定义在 timbre_data.cpp（由 scratchpad/gen_timbre.py 生成）。
// 这里的 extern 是必须的：文件作用域的 const 在 C++ 里是内部链接，
// 不声明 extern 的话生成文件里那个定义链接不到。
extern const Voice kVoices[];
extern const size_t kVoiceCount;

// 当前音色。只存在 RAM 里，不写闪存 —— 这是个试听用的开关。
const Voice &current();
void next();

}  // namespace timbre
