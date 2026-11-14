# AGENTS app Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cardputer 上新增 AGENTS app，经蓝牙显示 Mac 上 iTerm2 里 claude / codex session 的状态，状态变成「该你了」时响铃闪屏，回车让 iTerm2 切到该 session。

**Architecture:** 三段：Hammerspoon（列 session、判状态、切 tab）⇄ Unix socket ⇄ agentbridge（Swift，launchd 常驻，CoreBluetooth central，只转发）⇄ BLE ⇄ Cardputer（NimBLE 外设，AGENTS app）。三段说同一套逐行文本协议。

**Tech Stack:** Arduino + M5Cardputer + NimBLE-Arduino 2.x（固件，C++17，Unity native 测试）；Swift 6 + CoreBluetooth（agentbridge）；Hammerspoon Lua 5.4（测试用 luajit）。

**Spec:** `docs/superpowers/specs/2026-11-14-agents-app-design.md`

## Global Constraints

- 两个仓库：固件和 agentbridge 在 `/Users/jialiu/Projects/stackm5`；Hammerspoon 代码在 `/Users/jialiu/.hammerspoon`（独立 git 仓库）
- 两个仓库都是公开 GitHub 仓库，提交作者必须是 `childe <rmself@qq.com>`（`git -c user.name=childe -c user.email=rmself@qq.com commit ...`）
- 固件：C++17；纯逻辑放 `lib/<name>/`，不依赖 Arduino，用 `make test` 在 Mac 上跑
- 固件：BLE 只初始化一次，不 deinit；设备名保持 `"Cardputer Remote"`
- 菜单：DIAG 永远最后，AGENTS 插在 DIAG 前；数字直达键 1–6 变 1–7
- 协议：UTF-8，字段 `\t` 分隔；BLE 上一次 write / notify 恰好是一条消息，不带 `\n`；socket 上每条消息以 `\n` 结尾
- 协议消息：`B`、`S\t<id>\t<agent>\t<state>\t<title>\t<dir>`（agent = `c`|`x`，state = `r`|`w`）、`E`、`F\t<id>`、`C\t1` / `C\t0`
- 单条 `S` 消息 ≤ 180 字节，超出时按 UTF-8 字符边界截断 `<title>`
- 运行中判定：标题首字符 ∈ U+25D0–25D3 或 U+2800–28FF，否则为「该你了」
- BLE UUID（本计划定死，三段共用）：
  - 服务 `a7c4f3e0-5b1d-4e8a-9c2f-1d3b6e8a0001`
  - 写特征（Mac → Cardputer，write）`a7c4f3e0-5b1d-4e8a-9c2f-1d3b6e8a0002`
  - notify 特征（Cardputer → Mac）`a7c4f3e0-5b1d-4e8a-9c2f-1d3b6e8a0003`
  - 两个特征都不要求加密
- socket 路径 `~/.cache/agentbridge.sock`；agentbridge 日志 `~/.cache/agentbridge.log`
- Hammerspoon：每 2 秒取一次列表（异步，`hs.task`），只在收到 `C\t1` 后运行；socket 断开每 5 秒重连
- Hammerspoon 代码要能在 Lua 5.4 运行，测试用 `/opt/homebrew/bin/luajit`（luajit 没有 `utf8` 库，核心模块不能依赖它）
- Python 只用 `uv run --with bleak`，不用 pip

## Review Focus

1. **断在半份列表上**：Mac 发了 `B` 和几条 `S` 后断开、重连后重新发完整列表 → 不能把上次残留的 `S` 混进来（Task 1 `test_reset_drops_partial_list`）
2. **截断落在多字节字符中间**：中文 / emoji 标题超长 → 截断后仍是合法 UTF-8 且 ≤ 180 字节（Task 5 `encodeList truncates on utf8 boundary`）
3. **列表为空**：没有任何 agent session（或 iTerm2 没开）→ 屏幕显示「没有 agent session」，回车不发任何消息（Task 1 `test_empty_list_has_no_selection`；Task 6 iTerm2 未运行时发空列表）
4. **重构后遥控器回连**：从没进过 AGENTS、只用遥控器 → 电视断开后仍能自动回连、按键照常（Task 2 手工检查）
5. **一端重启**：agentbridge 或 Hammerspoon 任一重启 → 5 秒内恢复，Hammerspoon 拿到当前 `C` 状态（Task 4 新客户端补发 `C`；Task 6 手工检查）

---

### Task 1: 纯逻辑库 `lib/agents`（协议解析 + 提醒 + 光标）

**Files:**
- Create: `lib/agents/agents.h`, `lib/agents/agents.cpp`
- Test: `test/test_agents/test_main.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace agents {
  enum class State : uint8_t { Running, Waiting };
  struct Session { std::string id; char agent; State state; std::string title; std::string dir; };
  class Model {
   public:
    bool feedLine(const std::string &line);   // 喂一条消息（可带尾部 \n，会去掉）；收到 E 提交时返回 true
    void reset();                             // 断开时调：清列表、缓冲、标记，下一份列表重新当基线
    const std::vector<Session> &sessions() const;
    const std::vector<std::string> &lastAlerts() const;  // 最近一次提交中 r→w 的 id，按列表顺序
    bool marked(const std::string &id) const; // 是否带 '!'
    int cursor() const;                       // -1 = 列表为空
    const Session *selected() const;          // 列表为空时 nullptr
    void moveCursor(int delta);               // 钳在 [0, n-1]；落到的那行去掉 '!'
    std::string focusSelected();              // 返回 "F\t<id>" 并去掉该行 '!'；无选中返回 ""
  };
  }
  ```

- [ ] **Step 1: 写失败的测试**（`test/test_agents/test_main.cpp`，Unity，风格照 `test/test_carousel`）

  辅助函数 `feedList(Model&, std::vector<std::string> lines)` 依次喂 `"B"`、各行、`"E"`。用例与断言：
  - `test_parses_full_list`：喂 `S\tid1\tc\tr\t标题一\t~/p` 和 `S\tid2\tx\tw\tT2\t~/q` → `sessions().size()==2`，`[0].agent=='c'`，`[0].state==Running`，`[1].state==Waiting`，`[0].title=="标题一"`，`[1].dir=="~/q"`
  - `test_list_replaced_only_on_E`：第一份含 id1；再喂 `B`、`S id2...` 不喂 `E` → `sessions()` 仍只有 id1；`feedLine("E")` 返回 true 后变成 id2
  - `test_drops_S_before_B_and_short_lines`：不先喂 `B` 的 `S` 行被丢弃；`B`、`S\tid1\tc\tr`（缺字段）、`E` → 列表为空
  - `test_first_list_is_baseline`：第一份 id1=w → `lastAlerts()` 为空，`marked("id1")==false`
  - `test_running_to_waiting_alerts_and_moves_cursor`：基线 id1=w、id2=r、id3=r；第二份 id1=w、id2=w、id3=w → `lastAlerts()=={"id2","id3"}`，两者 `marked` 为真，`selected()->id=="id2"`
  - `test_new_session_does_not_alert`：基线 id1=r；第二份 id1=r、id9=w → `lastAlerts()` 为空
  - `test_cursor_follows_id_across_reorder`：基线 id1,id2,id3 全 r，`moveCursor(+1)` 选中 id2；第二份顺序 id3,id2,id1 → `selected()->id=="id2"`
  - `test_cursor_stays_on_row_when_session_vanishes`：基线 id1,id2,id3，光标在 id3（行 2）；第二份 id1,id2 → `cursor()==1`
  - `test_move_clears_mark_and_clamps`：制造 id2 带标记、光标在 id1；`moveCursor(+1)` → `marked("id2")==false`；`moveCursor(+5)` → `cursor()==n-1`；`moveCursor(-9)` → `cursor()==0`
  - `test_focus_selected`：光标在 id2 且带标记 → `focusSelected()=="F\tid2"`，`marked("id2")==false`
  - `test_empty_list_has_no_selection`：喂 `B`、`E` → `cursor()==-1`，`selected()==nullptr`，`focusSelected()==""`，`moveCursor(1)` 不崩
  - `test_reset_drops_partial_list`：基线 id1；喂 `B`、`S id7...`；`reset()`；再喂完整 id2=w → `sessions()` 只有 id2，`lastAlerts()` 为空（reset 后重新当基线）
  - `test_strips_trailing_newline`：`feedLine("E\n")` 同样提交

- [ ] **Step 2: 跑测试确认失败**

  Run: `make test`
  Expected: `test_agents` 编译失败（`agents.h` 不存在），其它测试仍 PASS

- [ ] **Step 3: 实现 `agents::Model`**（`lib/agents/agents.cpp`）

  提交时：新列表里每个 id 若在旧列表中是 Running、新列表中是 Waiting → 进 `lastAlerts`、加标记；基线（reset 后第一次提交）不产生提醒。光标规则：有提醒时跳到 `lastAlerts()[0]` 那一行（**不**清它的标记）；否则按原 id 找新行号，找不到就留在原行号并钳到末行；空列表为 -1；从空变非空为 0。消失的 id 从标记集合删掉。

- [ ] **Step 4: 跑测试确认通过**

  Run: `make test`
  Expected: 全部 PASS

- [ ] **Step 5: 提交**

  ```bash
  git add lib/agents test/test_agents
  git commit -m "feat(agents): 协议解析和提醒逻辑"
  ```

---

### Task 2: 共享 BLE 模块，遥控器改用它

**Files:**
- Create: `src/ble.h`, `src/ble.cpp`
- Modify: `src/remote_app.cpp`（`ServerCb` 93–115 行、`begin()` 184–218 行、`gConnected` 的读写）, `src/remote_app.h`
- Create (stub, Task 3 填充): `src/agents_app.h`, `src/agents_app.cpp` —— 本任务只实现 `agents_app::registerService()` 和写入回调排队

**Interfaces:**
- Produces:
  ```cpp
  namespace ble {
  void begin();                      // 幂等。第一次：init("Cardputer Remote")、安全参数照旧、createServer、
                                     // server->advertiseOnDisconnect(false)、remote_app::registerService(server)、
                                     // agents_app::registerService(server)、server->start()
  void setRemoteActive();            // 遥控器 begin() 时调，之后一直为真
  void setAgentsActive(bool on);     // AGENTS 进入 / 退出时调；off 时断开 Mac 连接
  void claimMac(uint16_t connHandle);// AGENTS 特征被写或被订阅时调，把这条连接记为 Mac
  bool macConnected();
  bool remoteConnected();            // 有任一已连接且不是 Mac 的连接
  bool notifyMac(const std::string &line);  // 用 notify(value,len) 重载发给 Mac 连接
  }
  namespace remote_app { void registerService(NimBLEServer *server); }   // 建 HID，原 begin() 里那段
  namespace agents_app { void registerService(NimBLEServer *server); }   // 建 AGENTS 服务和两个特征
  ```
- 广播规则（每次连接 / 断开 / setXxx 后重算）：`needRemote = remoteActive && !remoteConnected()`，`needAgents = agentsActive && !macConnected()`；两者都假则停广播；否则广播里放需要的服务 UUID（HID 16-bit、AGENTS 128-bit），名字放 scan response，appearance 仍 0x03C1，然后开始广播
- HID 服务必须先于 AGENTS 服务注册

- [ ] **Step 1: 抽出 `ble` 模块，HID 建立代码搬到 `remote_app::registerService()`**；`remote_app::begin()` 改为 `ble::begin(); ble::setRemoteActive();`；`gConnected` 的读处改成 `ble::remoteConnected()`；连接计数和断开原因的调试计数留在 `remote_app`，由 `ble` 的 server 回调转调 `remote_app` 的一个内部钩子（只统计非 Mac 连接即可，断开时按 handle 判断）
- [ ] **Step 2: `agents_app::registerService()`**：建服务和两个特征（写特征属性 WRITE，notify 特征属性 NOTIFY），写回调里 `ble::claimMac(connInfo.getConnHandle())` 并把值放进一个加锁的行队列（`std::deque<std::string>` + `portMUX` 或 FreeRTOS 队列，最多保留 64 条，满了丢最旧的）；订阅回调里也 `claimMac`
- [ ] **Step 3: 编译**

  Run: `make build`
  Expected: 成功；记下 app 槽占用百分比

- [ ] **Step 4: 烧写并手工检查遥控器（Review Focus 4）**

  先按 memory 规则确认设备：`esptool.py flash_id` 显示 8MB 再烧。
  Run: `make flash adv`
  检查：进 TV REMOTE → 电视能连、方向键和音量有效；在电视上断开蓝牙后等 10 秒 → 自动回连、按键仍有效；串口 `make monitor` 里没有 assert / 重启

- [ ] **Step 5: 提交**

  ```bash
  git add src/ble.h src/ble.cpp src/remote_app.cpp src/remote_app.h src/agents_app.h src/agents_app.cpp
  git commit -m "refactor: 抽出共享 BLE 模块，预注册 AGENTS 服务"
  ```

---

### Task 3: AGENTS app 页面 + 菜单接入 + 调试探针

**Files:**
- Modify: `src/agents_app.h`, `src/agents_app.cpp`
- Modify: `src/main.cpp`（`Page` 枚举 96 行、`draw()` 分派 ~540 行、`launch()` 582 行、按键分派 ~680 行、`loop()` 里的 tick）
- Modify: `src/menu_app.h:14`（`App` 枚举加 `Agents`，在 `Diag` 前）, `src/menu_app.cpp`（`kItems` 在 DIAG 前加一项、新图标函数、提示行里的 `1-6` 改 `1-7`、数字键处理范围）
- Modify: `README.md`（顶部菜单说明、app 表格、新增 AGENTS 一节）
- Create: `tools/agents_probe.py`

**Interfaces:**
- Consumes: Task 1 `agents::Model`；Task 2 `ble::*`
- Produces:
  ```cpp
  namespace agents_app {
  void registerService(NimBLEServer *server);  // Task 2 已有
  void begin();   // ble::begin(); ble::setAgentsActive(true)
  void end();     // ble::setAgentsActive(false); model.reset()
  bool tick();    // 取队列里的行喂 model；检测 Mac 连接变化（断开 → reset）；推进转圈和闪屏；返回是否要重绘
  void draw(LovyanGFX &g);
  bool handleChar(char c);   // ';' 上移、'.' 下移、'`' 返回 false
  bool handleEnter();        // focusSelected() 非空则 ble::notifyMac()
  }
  ```

- [ ] **Step 1: 页面实现**（按 spec「画面」一节）

  标题行 `AGENTS`、右侧 `● Mac` / `○ Mac` 和 session 数；正文每行：自绘状态符（运行中 = 半圆转圈，按 `millis()/150` 换 4 个方向；该你了 = ✳ 形状或实心点）、`C`/`X`、标题、带标记时行尾 `!`；选中行前 `▶` 或反色；倒数第二行当前项 `dir`；最后一行提示 `;. 选择  ENT 跳转  \` 返回`。标题和目录用 `fonts::efontCN_14`（显示不下时选 12），超长截断加 `…`。session 多于一屏时随光标滚动。未连接显示「等待 Mac 连接…」，已连接但空列表显示「没有 agent session」。
  提醒：`feedLine` 返回 true 且 `lastAlerts()` 非空时 `M5Cardputer.Speaker.tone(...)` 响一次，并在接下来 900ms 内每 150ms 交替整屏填色（参考 `noise_app.cpp:211`），共闪 3 次。运行中转圈需要定期重绘：页面在前台时每 150ms 置脏一次。

- [ ] **Step 2: 菜单与 main 接入**；`launch()` 加 `App::Agents` → `agents_app::begin(); gPage = Page::Agents;`；离开时调 `agents_app::end()`

- [ ] **Step 3: 写 `tools/agents_probe.py`**（bleak）

  按服务 UUID 扫描并连接；订阅 notify 特征并打印收到的每条消息；按 `--list` 参数依次写入：基线一份（id1=r、id2=w，标题含中文）、3 秒后第二份（id1=w）。用法写在文件头：`uv run --with bleak python tools/agents_probe.py`

- [ ] **Step 4: 编译、测试、烧写**

  Run: `make test && make build && make flash adv`
  Expected: 测试全过；编译成功

- [ ] **Step 5: 手工检查**

  1. 菜单按 `6` 进 AGENTS，屏幕显示「等待 Mac 连接…」；按 `7` 进 DIAG
  2. 在 iTerm 里跑探针 → 屏幕先显示两行（中文标题正常），3 秒后响一声、闪 3 次、光标跳到 id1 并显示 `!`
  3. 按回车 → 探针打印 `F\tid1`，`!` 消失
  4. 按 `` ` `` 回菜单 → 探针报告断开
  5. 进 TV REMOTE，确认 Task 2 的遥控器检查仍然通过

- [ ] **Step 6: 提交**

  ```bash
  git add src lib README.md tools/agents_probe.py
  git commit -m "feat: AGENTS app，在 Cardputer 上看 claude/codex session"
  ```

---

### Task 4: agentbridge（Swift，launchd）

**Files:**
- Create: `tools/agentbridge/main.swift`, `tools/agentbridge/Info.plist`, `tools/agentbridge/com.childe.agentbridge.plist`（LaunchAgent 模板）
- Modify: `Makefile`（`agentbridge`、`agentbridge-install`、`agentbridge-uninstall` 目标，`help` 里列出）, `.gitignore`（`tools/agentbridge/build/`）
- Modify: `README.md`（AGENTS 一节加安装说明，写明重新编译后要重新授权蓝牙）

**Interfaces:**
- Consumes: Global Constraints 里的 UUID、socket 路径、协议
- Produces: 常驻进程，socket 行为如下（Task 6 依赖）：客户端连上后立即收到当前 `C\t1` 或 `C\t0`；之后每次 BLE 连接状态变化发一次；Cardputer 的每条 notify 原样加 `\n` 转发；客户端发来的每一行（去掉 `\n`）按顺序 write-with-response 给 Cardputer，上一条回 `didWriteValueFor` 后才发下一条；Cardputer 未连接时丢弃客户端发来的行

- [ ] **Step 1: 写 `Info.plist`**：`CFBundleIdentifier` = `com.childe.agentbridge`，`NSBluetoothAlwaysUsageDescription` = `Relay iTerm2 agent session status to the Cardputer.`
- [ ] **Step 2: 写 `main.swift`**：`CBCentralManager` 跑在一个串行 `DispatchQueue` 上；`poweredOn` 后按服务 UUID 扫描，发现即停扫并连接，发现特征后订阅 notify；断开后重新扫描。Unix socket 服务端用 POSIX `socket/bind/listen` + `DispatchSource`，启动时 `unlink` 旧路径，同一时刻一个客户端、新连接关掉旧的；按 `\n` 切行（保留不完整的尾巴到下次）。所有状态只在那一个串行队列上改
- [ ] **Step 3: Makefile 目标**

  ```make
  agentbridge:
  	mkdir -p tools/agentbridge/build
  	swiftc -O tools/agentbridge/main.swift -o tools/agentbridge/build/agentbridge \
  	  -Xlinker -sectcreate -Xlinker __TEXT -Xlinker __info_plist -Xlinker tools/agentbridge/Info.plist
  	codesign -s - -f -i com.childe.agentbridge tools/agentbridge/build/agentbridge
  ```
  `agentbridge-install`：依赖 `agentbridge`，拷到 `~/.local/bin/agentbridge`，把 LaunchAgent 模板里的 `__HOME__` 换成 `$HOME` 写到 `~/Library/LaunchAgents/com.childe.agentbridge.plist`（`RunAtLoad`、`KeepAlive`、stdout/stderr → `~/.cache/agentbridge.log`），先 `launchctl bootout gui/$(id -u)/com.childe.agentbridge 2>/dev/null || true` 再 `launchctl bootstrap gui/$(id -u) <plist>`。`agentbridge-uninstall` 反向操作

- [ ] **Step 4: 编译**

  Run: `make agentbridge && otool -s __TEXT __info_plist tools/agentbridge/build/agentbridge | head -3`
  Expected: 编译成功；otool 能看到嵌入的 plist 段

- [ ] **Step 5: 前台手工检查（在 iTerm 里跑，蓝牙权限算 iTerm 的）**

  1. 终端 A：`tools/agentbridge/build/agentbridge`
  2. 终端 B：`nc -U ~/.cache/agentbridge.sock` → 立即看到 `C	0`
  3. Cardputer 进 AGENTS → 终端 B 看到 `C	1`，Cardputer 标题行变 `● Mac`
  4. 终端 B 粘贴 `B`、`S	id1	c	w	测试	~/x`、`E`（每行回车，字段用真 tab）→ Cardputer 显示一行
  5. Cardputer 按回车 → 终端 B 看到 `F	id1`
  6. 关掉终端 B 再重连 → 立即看到 `C	1`（Review Focus 5）

- [ ] **Step 6: 安装检查**

  Run: `make agentbridge-install`，在弹出的蓝牙授权框里点允许
  Expected: `launchctl print gui/$(id -u)/com.childe.agentbridge` 显示 `state = running`；重复 Step 5 的 2–3 步通过；`~/.cache/agentbridge.log` 里没有崩溃

- [ ] **Step 7: 提交**

  ```bash
  git add tools/agentbridge Makefile .gitignore README.md
  git commit -m "feat: agentbridge，在 Unix socket 和 Cardputer 蓝牙之间转发"
  ```

---

### Task 5: Hammerspoon 核心函数（`~/.hammerspoon` 仓库）

**Files:**
- Modify: `/Users/jialiu/.hammerspoon/iterm_agents_core.lua`
- Test: `/Users/jialiu/.hammerspoon/tests/iterm_agents_core_test.lua`

**Interfaces:**
- Produces:
  ```lua
  core.state(title) -> "r" | "w"
  core.cleanTitle(title) -> string        -- 去掉首个状态符及其后空格；去掉尾部 " (<job>) — <path>"
  core.encodeList(sessions, home) -> { "B", "S\t...", ..., "E" }   -- 只含 s.agent ~= nil 的项，保持输入顺序
  ```
  `sessions` 元素是 `classify()` 的输出（字段 `id`、`agent`、`title`、`cwd`）。`<agent>`：`claude`→`c`，`codex`→`x`。`<dir>`：`cwd` 以 `home` 开头时换成 `~`；`cwd` 为 nil 时空串。

- [ ] **Step 1: 写失败的测试**（追加到 `tests/iterm_agents_core_test.lua`，用文件里已有的 `assertEqual`）

  - `state`：`"◐ x"`、`"◓ x"`、`"⠸ x"`、`"⣿ x"` → `"r"`；`"✳ x"`、`"Respond to greeting | stackm5"`、`""` → `"w"`
  - `cleanTitle`：`"✳ 电路图项目增加题目 (claude) — ~/Projects/saisai.me"` → `"电路图项目增加题目"`；`"⠸ 提供项目卡供选择 | stackm5 (codex) — ~/Projects/stackm5"` → `"提供项目卡供选择 | stackm5"`；`"Respond to greeting | stackm5 (codex) — ~/Projects/stackm5"` → `"Respond to greeting | stackm5"`；`"◑ a (b) c (caffeinate) — ~/x"` → `"a (b) c"`
  - `encodeList`：两个 agent session 加一个非 agent → 返回 4 项，`[1]=="B"`，`[4]=="E"`，`[2]=="S\tid1\tc\tr\tT1\t~/p"`（cwd=`/Users/u/p`，home=`/Users/u`）
  - `encodeList` 清洗：标题含 `\t` 和 `\n` → 输出里被替换成空格
  - `encodeList truncates on utf8 boundary`：标题是 100 个「汉」（300 字节）→ `S` 行 `#line <= 180`，且最后一个字段能被逐字符解析（每个字符都以 `\xE6` 开头的 3 字节完整序列结束，即 `(#title) % 3 == 0`）

- [ ] **Step 2: 跑测试确认失败**

  Run: `cd /Users/jialiu/.hammerspoon && /opt/homebrew/bin/luajit tests/iterm_agents_core_test.lua`
  Expected: FAIL，`attempt to call field 'state' (a nil value)`

- [ ] **Step 3: 实现三个函数**。`state` 按字节判断（不用 `utf8` 库）：首字节 `0xE2`，且第二字节 `0x97`、第三字节 `0x90`–`0x93`（U+25D0–25D3），或第二字节 `0xA0`–`0xA3`（U+2800–28FF）。截断时从 180 字节处往回退到不是 `0x80`–`0xBF` 的字节为止

- [ ] **Step 4: 跑测试确认通过**

  Run: `cd /Users/jialiu/.hammerspoon && /opt/homebrew/bin/luajit tests/iterm_agents_core_test.lua && /opt/homebrew/bin/luajit tests/iterm_agents_integration_test.lua`
  Expected: 两个都无报错退出

- [ ] **Step 5: 提交**（在 `~/.hammerspoon` 仓库）

  ```bash
  git add iterm_agents_core.lua tests/iterm_agents_core_test.lua
  git commit -m "feat: iTerm2 agent 状态判断和 Cardputer 列表编码"
  ```

---

### Task 6: Hammerspoon 异步取列表 + 蓝牙桥客户端

**Files:**
- Modify: `/Users/jialiu/.hammerspoon/iterm_agents.lua`（新增 `M.sessionsAsync`）
- Create: `/Users/jialiu/.hammerspoon/iterm_agents_ble.lua`
- Modify: `/Users/jialiu/.hammerspoon/init.lua`（在 `require "iterm_agents"` 后加 `require "iterm_agents_ble"`）
- Test: `/Users/jialiu/.hammerspoon/tests/iterm_agents_integration_test.lua`

**Interfaces:**
- Consumes: Task 5 `core.encodeList`；Task 4 socket 行为；已有 `M.focus(id)`
- Produces:
  ```lua
  iterm_agents.sessionsAsync(callback)   -- callback(sessions)；iTerm2 未运行或任一步失败时 callback({})
  ```
  实现：`hs.task` 依次跑 `/usr/bin/osascript -e <listScript>`、`/bin/ps -A -o pid=,ppid=,etime=,tty=,comm=`、有 agent 时 `/usr/sbin/lsof -a -d cwd -Fn -p <pids>`，在最后一步回调里走 `core.parseSessions` / `parseProcesses` / `classify`。`sessions()` 和菜单行为不变。

- [ ] **Step 1: 写失败的测试**（追加到 integration 测试，mock `hs.task.new(path, cb, args)` 返回带 `:start()` 的对象，`start` 时按 path 同步调 `cb(0, 预置输出, "")`）

  - `sessionsAsync` 用预置的 osascript / ps / lsof 输出 → callback 收到与同步 `sessions()` 相同顺序的 id 列表
  - iTerm2 未运行（`state.running=false`）→ callback 收到空表，且没有任何 task 被创建

- [ ] **Step 2: 跑测试确认失败**

  Run: `cd /Users/jialiu/.hammerspoon && /opt/homebrew/bin/luajit tests/iterm_agents_integration_test.lua`
  Expected: FAIL，`sessionsAsync` 为 nil

- [ ] **Step 3: 实现 `sessionsAsync`**

- [ ] **Step 4: 跑测试确认通过**

  Run: 同 Step 2
  Expected: 无报错退出

- [ ] **Step 5: 实现 `iterm_agents_ble.lua`**（依赖真实 `hs.socket`，不写单测）

  - `hs.socket.new()` 连 `os.getenv("HOME") .. "/.cache/agentbridge.sock"`；用 `:read("\n")` 逐行读，回调里处理完再发下一次 `read`
  - 连接失败或断开：停推送定时器，5 秒后重连（`hs.timer.doAfter`）
  - `C\t1`：立即推一次，并起 `hs.timer.doEvery(2, push)`；`C\t0`：停定时器
  - `push`：上一轮还在跑就跳过；否则 `sessionsAsync`，回调里把 `core.encodeList(sessions, HOME)` 每项加 `\n` 拼起来一次 `write`
  - `F\t<id>`：`iterm_agents.focus(id)`
  - 模块表存进全局（`_G.itermAgentsBle = M`），避免 socket 和 timer 被回收

- [ ] **Step 6: 端到端手工检查**

  1. Hammerspoon 控制台 Reload Config，无报错
  2. Cardputer 进 AGENTS → 2 秒内列出当前所有 claude / codex session，顺序和菜单栏 🤖 列表的 agent 部分一致，中文标题正常
  3. 在某个 claude session 里发一条短提示 → Cardputer 上该行变成转圈；跑完后响一声、闪 3 次、光标跳到该行并显示 `!`
  4. 按回车 → iTerm2 切到该 tab 并置前
  5. `launchctl kickstart -k gui/$(id -u)/com.childe.agentbridge` → 10 秒内 Cardputer 重新出现列表（Review Focus 5）
  6. 退出 iTerm2 → Cardputer 显示「没有 agent session」（Review Focus 3）
  7. Cardputer 回菜单 → Hammerspoon 不再跑 osascript（活动监视器里 osascript 不再每 2 秒出现）

- [ ] **Step 7: 提交**（在 `~/.hammerspoon` 仓库）

  ```bash
  git add iterm_agents.lua iterm_agents_ble.lua init.lua tests/iterm_agents_integration_test.lua
  git commit -m "feat: 把 iTerm2 agent 列表推送到 Cardputer"
  ```
