// 主音量档位。
//
// 为什么用离散档位而不是 ±1 连续调：M5Unified 的音量是 0~255 的线性系数，
// 人耳对响度是对数感知的，连续加减在中段几乎听不出变化、按几十下才有用。
// 九档一按一个台阶，最高档就是满幅（之前硬编码的 255）。
//
// 只存在 RAM 里，不写闪存 —— 和音色、可视化风格一致。
#pragma once

namespace volume {

// 把当前档位应用到喇叭。setup() 里调一次。
void begin();

int level();     // 0..levelMax()，0 = 静音
int levelMax();  // 最高档的下标

void up();
void down();

}  // namespace volume
