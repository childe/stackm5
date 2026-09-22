// 简谱解析器 —— 纯 C++，零硬件依赖，可在电脑上单元测试。
// 语法规范见 docs/superpowers/specs/2026-09-17-jianpu-player-design.md
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace music {

// 一个音符
struct Note {
    uint8_t step = 0;        // 1-7 = 简谱音级；0 = 休止符
    int8_t octave = 0;       // 相对中音的八度偏移：+1 = 高八度
    int8_t accidental = 0;   // -1 降半音 / 0 本位 / +1 升半音
    float beats = 1.0f;      // 时长，单位「拍」
    uint16_t srcPos = 0;     // 在原文中的起始下标（播放高亮 / 错误定位用）
    uint16_t srcLen = 0;     // 在原文中占的字符数
};

// 谱子的头部信息
struct Header {
    int8_t keyRoot = 0;  // 调号主音相对 C 的半音数：C=0、D=2、F=5 …
    int bpm = 120;       // 速度
};

// 解析结果。ok == false 时 pos 指向第一个出错的字符
struct ParseError {
    bool ok = true;
    uint16_t pos = 0;
    const char *reason = "";
};

struct Score {
    Header header;
    std::vector<Note> notes;
    ParseError error;

    float totalBeats() const;
};

// 解析一段简谱文本
Score parse(const char *text, size_t len);

// 音级 → 频率（Hz）。休止符返回 0
float noteToFreq(const Note &n, const Header &h);

// 头部行（含结尾换行）占多少字符；没有头部行时返回 0。
// 存盘时头部和正文都写进文件，但编辑器只编辑正文，所以要能切开。
size_t headerPrefixLen(const char *text, size_t len);

// 把谱子摊成一条时间轴：每个音符从第几毫秒开始、响多久。
// 这部分是纯计算，播放器和进度条都用它。
struct Timeline {
    std::vector<uint32_t> onsetMs;  // 第 i 个音符的起始时刻
    std::vector<uint32_t> holdMs;   // 第 i 个音符实际发声的长度（留了间隙）
    uint32_t totalMs = 0;
};

Timeline buildTimeline(const Score &s);

// elapsedMs 时刻应该在响第几个音符。返回 -1 表示已经播完。
int indexAt(const Timeline &t, uint32_t elapsedMs);

}  // namespace music
