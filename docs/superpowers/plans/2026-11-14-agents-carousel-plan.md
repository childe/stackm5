# AGENTS 走马灯和筛选 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** AGENTS 页加走马灯视图（每秒一个 session，只显示状态符号 + 标题）、`f` 切筛选、`v` 切视图、空格跳转。

**Architecture:** 筛选、光标、轮换节奏放进 `lib/agents`（纯逻辑，native 单测）；`src/agents_app.cpp` 只负责按键和绘制。

**Tech Stack:** C++17，Unity native 测试，M5GFX。

**Spec:** `docs/superpowers/specs/2026-11-14-agents-carousel-design.md`

## Global Constraints

- 提交作者 `childe <rmself@qq.com>`
- `Model` 默认筛选是 All，现有 16 个测试不改语义、必须继续通过
- 轮换间隔 1000ms；手动翻页 / 提醒后暂停 5000ms
- 自动前进不清 `!`；手动 `moveCursor` 和 `focusSelected` 清
- 按键：空格 / ENT 跳转，`v` 视图，`f` 筛选，`;` `.` 上下，`` ` `` 返回

## Review Focus

1. **筛选后没有可见项**：只看「该你了」时全在跑 → `selected()==nullptr`，跳转不发消息，走马灯显示「都在跑（N 个）」（Task 1 `test_waiting_filter_empty`）
2. **轮换到一半列表变短**：当前下标越界 → 钳到末尾，不越界访问（Task 1 `test_cursor_clamped_when_visible_shrinks`）
3. **切筛选时当前项被过滤掉**：留在原位置并钳住（Task 1 `test_filter_change_keeps_selection_or_row`）
4. **长中文标题**：超过 3 行截断加 `…`，不切半个字（Task 2 实机检查）
5. **millis() 回绕**：`Rotation` 用无符号差值比较（Task 1 `test_rotation_survives_wraparound`）

---

### Task 1: `lib/agents` 筛选、循环前进、轮换节奏

**Files:**
- Modify: `lib/agents/agents.h`, `lib/agents/agents.cpp`
- Test: `test/test_agents/test_main.cpp`

**Interfaces:**
- Produces:
  ```cpp
  enum class Filter : uint8_t { Waiting, All };
  // Model 新增：
  void setFilter(Filter f);
  Filter filter() const;
  std::vector<const Session *> visible() const;   // 按 sessions() 顺序，符合筛选的
  void advance();                                 // 在可见项里 +1，末尾回到 0；不清标记
  // 语义变化：cursor() / selected() / moveCursor() / focusSelected() 都作用于 visible()
  class Rotation {
   public:
    static constexpr uint32_t kStepMs = 1000;
    static constexpr uint32_t kHoldMs = 5000;
    bool due(uint32_t now);      // 到点返回 true 并把下一次排到 now + kStepMs
    void hold(uint32_t now);     // 下一次排到 now + kHoldMs
  };
  ```

- [ ] **Step 1: 写失败的测试**（追加）
  - `test_waiting_filter_shows_only_waiting`：id1=r,id2=w,id3=w，`setFilter(Waiting)` → `visible()` 是 id2,id3；`selected()->id=="id2"`
  - `test_waiting_filter_empty`：全 r，Waiting → `visible()` 空，`cursor()==-1`，`focusSelected()==""`
  - `test_filter_change_keeps_selection_or_row`：All 下选中 id3（行 2，id1=r,id2=w,id3=w）→ Waiting 后仍选 id3（可见行 1）；All 下选中 id1（r）→ Waiting 后 `cursor()==0`
  - `test_advance_wraps_and_keeps_marks`：制造 id2 带标记，光标在最后一项，`advance()` → `cursor()==0`；再 advance 到 id2 → `marked("id2")` 仍为真
  - `test_cursor_clamped_when_visible_shrinks`：Waiting 下 3 个可见、光标在 2；新列表只剩 1 个 w → `cursor()==0`
  - `test_alert_selects_alerted_under_waiting_filter`：Waiting 下基线 id1=w,id2=r；第二份 id2=w → `selected()->id=="id2"`
  - `test_rotation_steps_every_second`：`Rotation r; r.due(0)` 为真；`r.due(999)` 假；`r.due(1000)` 真
  - `test_rotation_hold_delays`：`r.due(0); r.hold(100)`；`r.due(4000)` 假，`r.due(5100)` 真
  - `test_rotation_survives_wraparound`：`r.due(0xFFFFFF00u)` 真；`r.due(0xFFFFFF00u + 999)` 假；`r.due(0xFFFFFF00u + 1000)`（回绕后）真
- [ ] **Step 2: 跑测试确认失败** — Run: `pio test -e native -f test_agents`；Expected: 编译失败（`Filter` / `Rotation` 未定义）
- [ ] **Step 3: 实现**。内部仍保存 `cursor_` 为可见下标；任何改变可见集合的操作（commit、setFilter）先记下当前选中 id 和行号，重算后按 id 找，找不到留原行钳住。`Rotation` 初始状态下第一次 `due()` 立即为真；比较用 `int32_t(now - next_) >= 0`
- [ ] **Step 4: 跑测试确认通过** — Run: `pio test -e native`；Expected: 全部 PASS（含原 16 个）
- [ ] **Step 5: 提交** — `git commit -m "feat(agents): 筛选、循环前进和轮换节奏"`

---

### Task 2: 走马灯绘制和按键

**Files:**
- Modify: `src/agents_app.cpp`, `src/agents_app.h`（头注释按键）, `README.md`（AGENTS 一节的画面和按键表）

**Interfaces:**
- Consumes: Task 1 全部

- [ ] **Step 1: 状态**：静态 `enum class View { Carousel, List }`，`gView = Carousel`、首次进页设 `gModel.setFilter(Waiting)`（之后保留用户选择：用一个 `gPrefsInit` 标志，只在第一次 `begin()` 设默认；`reset()` 不能把筛选冲掉 —— Task 1 的 `reset()` 不碰筛选）
- [ ] **Step 2: 按键**：`handleChar`：`' '` → 跳转（同 `handleEnter`）；`v` 切视图；`f` 切筛选；`;` `.` → `moveCursor(∓1)`，走马灯下再 `gRot.hold(now)`；提醒触发时 `gRot.hold(now)`
- [ ] **Step 3: tick**：走马灯视图且 `gRot.due(now)` 时 `gModel.advance()` 并置脏
- [ ] **Step 4: 走马灯绘制**：顶栏同列表视图，右侧改成「该你了 i/n」或「全部 i/n」；正文：左侧 2 倍大小的状态符号（中心约 (16, 52)），标题用 `fonts::efontCN_24` 从 x=36 起按像素折行，最多 3 行，第 3 行放不下时截断加 `…`；无可见项时显示「都在跑（N 个）」（N = 全部 session 数）或未连接文案；底部提示 `SPC 跳转  v 列表  f 全部`（筛选是 All 时最后一项写 `f 该你了`）。列表视图提示改成 `;. 选择 SPC 跳转 v 走马灯 f …`，放不下就缩写，用 `textWidth` 量
- [ ] **Step 5: 编译** — Run: `make build`；Expected: 成功，记下 Flash 占用（efontCN_24 若让占用超过 70%，改用 efontCN_14 + `setTextSize(2)` 并 ledger）
- [ ] **Step 6: 烧写前确认设备** — esptool `flash_id` 显示 8MB；Run: `make flash adv`
- [ ] **Step 7: 实机检查**：进 AGENTS 默认走马灯 + 只看该你了，每秒换一张；`f` 后变全部、转圈卡片出现；`v` 切列表且筛选保持；`.` 翻页后 5 秒不自动换；空格让 iTerm2 切到当前卡片的 session；长标题折行截断正常
- [ ] **Step 8: 提交** — `git commit -m "feat(agents): 走马灯视图，f 切筛选，v 切视图，空格跳转"`
