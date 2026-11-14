// 主音量档位。
//
// 为什么用离散档位而不是 ±1 连续调：M5Unified 的音量是 0~255 的线性系数，
// 人耳对响度是对数感知的，连续加减在中段几乎听不出变化、按几十下才有用。
// 九档一按一个台阶，最高档就是满幅（之前硬编码的 255）。
//
// 断电保存，1.5 秒无变化写一次 NVS（命名空间 cfg，键 vol）—— 连按只写一次，省 Flash 擦写。
#pragma once

namespace volume {

// 从 NVS 读档位（没有则满档）并应用到喇叭。setup() 里调一次。
void begin();

// loop 每圈调一次：档位改动后静默 1.5 秒写一次 NVS。
void poll();

int level();     // 0..levelMax()，0 = 静音
int levelMax();  // 最高档的下标

void up();
void down();

}  // namespace volume
