// 提示音：两个音上行（E6 → A6），各 120ms。走当前音色的波表，和编辑器试听同一条路。
//
// AGENTS 「该你了」时响它；CONFIG 调音量时也响它 —— 试听用设备上最常听到的那个声音，
// 调到合适就不会在提醒时被吓一跳（880Hz 短音在这块小喇叭上比它轻得多）。
#pragma once

namespace chime {

// 从第一个音开始响；正在响的会被打断重来
void play();

// loop 每圈调一次：到点切第二个音、响完收尾
void poll();

}  // namespace chime
