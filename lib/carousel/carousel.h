// 首页横向轮播菜单的纯逻辑 —— 零硬件依赖，可以在电脑上单元测试。
//
// 一圈 n 个 item，屏幕正中是当前项，左右各露一个邻居。转一格时画面滑动
// kStepMs 毫秒：selected 立刻变，视觉偏移 offset 从 ±1 缓动到 0。
// offset 完全由时间算出来，所以离开菜单再回来，看到的一定是停稳的画面。
//
// 设计见 docs/superpowers/specs/2026-09-29-carousel-menu-design.md
#pragma once

#include <cstdint>

namespace carousel {

constexpr uint32_t kStepMs = 160;
constexpr float kMaxLag = 1.5f;  // 连按时视觉最多落后几格，超出的直接跳过
constexpr int kSlotSpacing = 80;  // 相邻格中心距（px）
constexpr int kCenterX = 120;
constexpr int kBigIcon = 40;  // 正中的图标边长
constexpr int kSmallIcon = 28;  // 两侧的图标边长
constexpr float kSideBright = 0.45f;

// 取模，负数也落在 [0, n)。n <= 0 返回 0
int wrap(int i, int n);

class Carousel {
   public:
    // count < 1 按 1 处理（运行期不会出现，只是不让取模除零）。
    // initial 取模后作为开机当前项
    explicit Carousel(int count, int initial = 0);

    int count() const { return _count; }
    int selected() const { return _sel; }

    // dir 只看正负：> 0 右转（下一个），< 0 左转；0 什么都不做
    void step(int dir, uint32_t nowMs);

    // 直接设当前项并停掉动画
    void jump(int index);

    // 剩余视觉偏移（格）。右转后从 +1 缓动到 0；0 表示停稳
    float offset(uint32_t nowMs) const;
    bool animating(uint32_t nowMs) const;

   private:
    int _count;
    int _sel = 0;
    float _startOffset = 0.0f;
    uint32_t _startMs = 0;
};

// item 在画面上的格位：0 = 正中，负数在左。取模时挑离正中最近的那一圈
// （同样近取非负那圈），返回 d + offset
float slotPos(int item, int sel, float offset, int n);

struct SlotGeom {
    int x;  // 图标中心
    int size;  // 图标边长
    float bright;  // 0..1，乘到主题色上
    bool visible;
};

SlotGeom slotGeom(float pos);

// RGB565 各通道乘 k 后四舍五入。k <= 0 返回 0，k >= 1 原样返回
uint16_t dim565(uint16_t c, float k);

}  // namespace carousel
