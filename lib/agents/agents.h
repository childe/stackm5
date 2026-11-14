// AGENTS app 的纯逻辑 —— 零硬件依赖，可以在电脑上单元测试。
//
// Mac 每 2 秒发一份完整的 session 列表（B / S… / E 逐条消息），这里负责：
// 拼出列表、和上一份比较找出「从运行中变成该你了」的 session（要提醒）、
// 维护 '!' 标记和光标。
//
// 设计见 docs/superpowers/specs/2026-11-14-agents-app-design.md
#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace agents {

enum class State : uint8_t { Running, Waiting };

struct Session {
    std::string id;
    char agent;  // 'c' = claude，'x' = codex
    State state;
    std::string title;
    std::string dir;
};

// 显示范围：只看「该你了」，或者全部
enum class Filter : uint8_t { Waiting, All };

// 光标、选中、移动都只在 visible()（符合筛选的 session）里进行
class Model {
   public:
    void setFilter(Filter f);
    Filter filter() const { return filter_; }

    // 按 sessions() 的顺序，符合筛选的那些
    std::vector<const Session *> visible() const;

    // 走马灯自动前进：可见项里 +1，末尾回到第一个。不清 '!'（没人看就不算看过）
    void advance();

    // 喂一条消息（可带尾部 \n）。收到 E、列表提交时返回 true
    bool feedLine(const std::string &line);

    // 断开时调：清列表、缓冲和标记，下一份列表重新当基线
    void reset();

    const std::vector<Session> &sessions() const { return list_; }

    // 最近一次提交里从 Running 变成 Waiting 的 id，按列表顺序
    const std::vector<std::string> &lastAlerts() const { return alerts_; }

    bool marked(const std::string &id) const { return marks_.count(id) > 0; }

    int cursor() const { return cursor_; }  // -1 = 列表为空
    const Session *selected() const;

    // 钳在 [0, n-1]；落到的那行去掉 '!'
    void moveCursor(int delta);

    // 返回 "F\t<id>" 并去掉该行 '!'；没有选中项返回 ""
    std::string focusSelected();

   private:
    void commit();
    // 可见集合变了之后重新定位光标：先找 target 这个 id，找不到就留在 prevRow（钳到末尾）
    void reselect(const std::string &target, int prevRow);

    Filter filter_ = Filter::All;
    std::vector<Session> list_;
    std::vector<Session> pending_;
    bool receiving_ = false;
    bool haveBaseline_ = false;
    std::vector<std::string> alerts_;
    std::set<std::string> marks_;
    int cursor_ = -1;
};

// 走马灯节奏：每 stepMs() 前进一格，faster() / slower() 按 1 秒调，钳在 1~10 秒。
// 手动翻页或提醒后 hold() 停两格，比一格长，手动翻过的才真算「停住」。
// 用无符号差值比较，millis() 回绕（约 49 天）也不出错
class Rotation {
   public:
    static constexpr uint32_t kDefaultMs = 3000;  // 实机试过：1 秒读不完，5 秒嫌慢
    static constexpr uint32_t kMinMs = 1000;
    static constexpr uint32_t kMaxMs = 10000;
    static constexpr uint32_t kAdjustMs = 1000;

    // 到点返回 true，并把下一次排到 now + stepMs()。第一次调用立即为 true
    bool due(uint32_t now);
    void hold(uint32_t now);

    uint32_t stepMs() const { return step_; }
    // 改间隔后，下一次从上一次前进的时刻按新间隔重排
    void faster();
    void slower();

   private:
    void setStep(uint32_t ms);

    bool started_ = false;
    uint32_t step_ = kDefaultMs;
    uint32_t last_ = 0;  // 上一次前进的时刻
    uint32_t next_ = 0;
};

}  // namespace agents
