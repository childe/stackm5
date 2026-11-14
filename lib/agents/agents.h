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

class Model {
   public:
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

    std::vector<Session> list_;
    std::vector<Session> pending_;
    bool receiving_ = false;
    bool haveBaseline_ = false;
    std::vector<std::string> alerts_;
    std::set<std::string> marks_;
    int cursor_ = -1;
};

}  // namespace agents
