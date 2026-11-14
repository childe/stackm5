# AGENTS app：在 Cardputer 上看 claude / codex session 并跳转 — Design

## 需求

电脑上同时开着好几个 iTerm2 tab 跑 claude / codex。想把 Cardputer 当副屏：

- 列出正在跑 claude / codex 的 session，以及各自的状态（运行中 / 该你了）
- 某个 session 从「运行中」变成「该你了」（跑完一轮，或者停下来等输入、等权限确认）时，
  Cardputer 响一声并闪屏
- 在 Cardputer 上选中一个 session 按回车，电脑上的 iTerm2 切到那个 tab

用户的选择：

- 连接方式用**蓝牙**
- **只在 AGENTS app 打开时**连接和提醒。在别的 app 里不提醒
- Mac 端逻辑放在 **Hammerspoon**，复用 `~/.hammerspoon/iterm_agents.lua` 已有的列 session 和切 tab 代码

## 非目标

- 其它 app 里的全局提醒（以后可能加，本次不做；本次代码要能复用到那一步）
- 区分「等权限确认」和「跑完了」：两者都算「该你了」
- iTerm2 以外的终端；多台 Mac

## 已验证的事实

**状态从 iTerm2 标题首字符判断**，不需要挂 Claude / Codex 的 hook（2026-11-14 实测）：

| agent | 运行中 | 该你了 |
|---|---|---|
| claude | `◐◑◒◓`（U+25D0–25D3）转圈 | `✳`（U+2733） |
| codex | 盲文点阵 `⠸⠦⠏…`（U+2800–28FF）转圈 | 无前缀，直接是标题 |

规则：**首字符属于 U+25D0–25D3 或 U+2800–28FF 就是运行中，否则是该你了。**
等权限确认时标题长什么样没有实测，按上面规则它会落到「该你了」，正是想要的结果。

**Hammerspoon 起的子进程碰蓝牙会被系统杀掉。** Hammerspoon.app 的 Info.plist 没有
`NSBluetoothAlwaysUsageDescription`，TCC 把它启动的子进程算在它头上。实测 `hs.task` 起
bleak 扫描，Python 直接崩溃（"Python quit unexpectedly"），而同一脚本在 iTerm 里正常扫到 21 个设备。
所以蓝牙必须放在一个**由 launchd 启动、自带用途说明**的独立进程里。

## 方案

### 考虑过的做法

- **A. Hammerspoon 管逻辑 + 独立蓝牙桥（选定）**：列 session、判状态、切 tab 留在 Hammerspoon，
  复用 `iterm_agents_core.lua` 和它的测试；蓝牙桥只做 socket ↔ 蓝牙的逐行转发。
- **B. 一个 Python 常驻进程包办**：自己调 osascript、自己连蓝牙。要把 Lua 的进程树解析重写一遍，
  而且 launchd 下的 Python 同样没有用途说明、同样会被杀。不选。
- A 的初版设想是 Hammerspoon 用 `hs.task` 起 Python 蓝牙桥、走 stdin/stdout。上面的实测否定了它，
  改成 Swift + launchd + Unix socket。

### 整体结构

```
iTerm2 ──AppleScript/ps── Hammerspoon ──Unix socket── agentbridge ──BLE── Cardputer (AGENTS app)
                          (逻辑)         (逐行文本)    (Swift, launchd)    (外设)
```

三段都说同一套**逐行文本协议**（下节）。agentbridge 不解析内容，只转发，外加报告蓝牙连接状态。

## 协议

UTF-8 文本，一行一条消息，字段用 `\t` 分隔。蓝牙上每条消息就是一次 write / notify，不跨包。

Mac → Cardputer（一次完整列表，每 2 秒发一次）：

```
B                                           列表开始
S <id> <agent> <state> <title> <dir>        一个 session；agent = c|x，state = r|w（运行中 / 该你了）
E                                           列表结束；Cardputer 收到 E 才用新列表替换旧列表
```

Cardputer → Mac：

```
F <id>                                      跳转到这个 session
```

agentbridge → Hammerspoon（不经过蓝牙）：

```
C 1  /  C 0                                 Cardputer 连上 / 断开
```

- `<id>` 是 iTerm2 session id（UUID），原样交回 `M.focus()`，那里已经做了字符白名单校验
- `<title>` 去掉首字符状态符号和后面的空格，也去掉 iTerm2 附加的 ` (claude) — ~/…` 尾巴
- `<dir>` 是 agent 进程 cwd（`classify()` 已经算出），把 `$HOME` 换成 `~`
- 单条 `S` 行超过 180 字节时，Hammerspoon 按 UTF-8 字符边界截断 `<title>`（180 留出 macOS
  常见 MTU 185 减 ATT 头 3 字节后的余量）
- 字段里出现 `\t` / `\n` 时替换成空格
- 列表只含 agent session（`s.agent ~= nil`），顺序沿用 `classify()`：按目录分组，组内最新在上

## Mac 端：Hammerspoon（`~/.hammerspoon` 仓库）

- `iterm_agents_core.lua` 加纯函数：
  - `M.state(title)` → `"r"` / `"w"`，按上表规则
  - `M.encodeList(sessions, home)` → 协议文本（含 B/E、清洗、截断）
  - 测试写进现有 `tests/iterm_agents_core_test.lua`
- 新模块 `iterm_agents_ble.lua`，`init.lua` 里 `require`：
  - 用 `hs.socket` 连 agentbridge 的 Unix socket；连不上或断开就每 5 秒重连
  - 收到 `C 1` 起 2 秒定时器，每次异步取一次 session 列表并发送 `encodeList`；收到 `C 0` 停定时器。
    Cardputer 没连时不跑 osascript。上一轮还没跑完就跳过这一轮
  - **必须异步**（实现的决定）：同步的 osascript + ps + lsof 实测合计约 0.4 秒，每 2 秒跑一次会占掉
    Hammerspoon 主线程约 20% 的时间，热键会卡。三个命令改用 `hs.task` 依次跑，回调里再走 `core.classify`
  - 收到 `F <id>` 调 `iterm_agents.focus(id)`
- `iterm_agents.lua` 新增 `M.sessionsAsync(callback)`，和 `sessions()` 共用解析代码；菜单栏仍用同步的
  `sessions()`，行为不变。`focus()` 已经在 `M` 上，直接用

## Mac 端：agentbridge（stackm5 仓库 `tools/agentbridge/`）

- Swift 命令行程序，CoreBluetooth central。用 `-Xlinker -sectcreate __TEXT __info_plist Info.plist`
  把带 `NSBluetoothAlwaysUsageDescription` 的 Info.plist 嵌进二进制，这样 launchd 启动它时 TCC 认它自己
- 监听 Unix socket `~/.cache/agentbridge.sock`（启动时先删旧文件）。同一时刻只接一个客户端，新连接顶掉旧的
- 扫描 AGENTS 服务 UUID，找到就连，订阅 notify 特征；断开后重新扫描
- socket 收到的每一行 → write 到 Cardputer 的写特征（with response，按顺序一条一条发）；
  Cardputer notify 的每一行 → 写回 socket
- 连上 / 断开时往 socket 写 `C 1` / `C 0`；新客户端接入时立即补发当前状态
- `make agentbridge` 编译；`make agentbridge-install` 拷到 `~/.local/bin/` 并装 LaunchAgent
  （`~/Library/LaunchAgents/…agentbridge.plist`，`RunAtLoad` + `KeepAlive`），然后 `launchctl bootstrap`。
  首次运行 macOS 会弹蓝牙授权框
- 日志写 stderr，LaunchAgent 把它重定向到 `~/.cache/agentbridge.log`
- 签名用 ad-hoc（`codesign -s -`，实现的决定，没有 Apple Developer 证书也能用）。TCC 按 cdhash 记授权，
  每次重新编译后要重新授权一次；agentbridge 很少改，可以接受。README 里写明这一点

## Cardputer 端

### 蓝牙协议栈共享

现状：`remote_app::begin()` 里 `NimBLEDevice::init("Cardputer Remote")`，建 server、挂 HID 服务、开广播，
只初始化一次、从不 deinit（`remote_app.cpp:186`）。

改成：抽一个公共的 `ble` 模块，第一次被任一 app 调用时（遥控器或 AGENTS，谁先谁触发）
`init` + `createServer`，**一次性注册 HID 和 AGENTS 两个服务，HID 在前**，然后 `server->start()`。
理由：NimBLE 2.x 在 `start()` 之后再加服务要重置 GATT 表，而且要等所有连接断开才生效，
电视连着时 AGENTS 服务出不来；HID 在前则 HID 的 handle 和现在一样，已绑定的电视缓存不会失效。
设备名保持 "Cardputer Remote"（改名会让已配对的电视认不出）。

代价（实现的决定，可接受）：本次开机没开过遥控器、直接进 AGENTS 时，GATT 里也有 HID，
已绑定的电视可能自动回连。它不会收到按键，无害。

连接要区分是谁：`ServerCb` 现在是全局的，Mac 连上会改遥控器的 `gConnected` 和计数。
改成按连接 handle 区分：在 AGENTS 写特征上收到写入、或 notify 特征被订阅的那条连接记为 Mac 连接，
其它连接归遥控器。遥控器的状态只看自己的连接。

广播状态由 `ble` 模块统一管：广播里带哪些 UUID = 遥控器是否已启动（带 HID）+ AGENTS 是否在前台（带 AGENTS）。
两者都没有就停广播。`onDisconnect` 不再无条件 `startAdvertising()`，而是按这个规则重算。

- AGENTS 服务：一个自定义 128-bit 服务 UUID，两个特征：写特征（Mac → Cardputer，write）
  和 notify 特征（Cardputer → Mac）。**不要求加密 / 配对**，Mac 不用走配对流程
- 进入 AGENTS app：把 AGENTS 服务 UUID 加进广播并开始广播
- 退出 AGENTS app：断开 Mac 那条连接，从广播里去掉 AGENTS UUID，按上面规则重算广播
  （遥控器没启动过就停广播）。电视的连接不受影响
- 遥控器 app 现有行为（断开后重新广播、HID 配对）不变

### 纯逻辑库 `lib/agents`（native 单元测试 `test/test_agents`）

- 增量解析协议行：`B` 开始缓冲，`S` 追加，`E` 提交；`B` 之前的 `S`、缺字段的行丢弃
- 列表提交时和上一份比较，返回「从 r 变成 w 的 session id」。规则：
  - 连接后收到的第一份列表只做基线，不提醒
  - 新出现的 session 不提醒（即使它是 w）；消失的 session 直接删掉
- 维护 `!` 标记集合：提醒时加上；该行被选中或被跳转时去掉；session 消失时去掉
- 光标按 session id 跟踪，列表重排后光标仍停在原来那个 session 上；那个 session 消失时光标留在原行号（越界则钳到末行）

### 画面（240×135）

```
AGENTS              ● Mac   7         标题行：连接状态 + session 数
 ◐ C Claude/Codex活动sessi…           每行：状态符 agent 标题（中文字库）
▶✳ C 电路图项目增加题目     !
 ⠿ X 提供项目卡供选择
 ✳ C 可用的api
 ✳ C Udf权限管控
~/Projects/saisai.me                  当前选中项的目录
;. 选择  ENT 跳转  ` 返回              提示行
```

- 状态符在 Cardputer 上自己画：运行中画转动的半圆（本地动画，不依赖 Mac 发来的字符），该你了画 `✳` 或等价图形
- agent：`C` = claude，`X` = codex
- 标题和目录要显示中文，需要链接一个中文字库（M5GFX 的 efontCN 系列之一）。README 说现在 app 槽用了约 38%（`platformio.ini` 注释里的 31.7% 是旧数据），加字库前先编译一次核实，空间够；
  选哪个字号、超长截断加 `…` 由实现决定
- session 多于一屏时列表随光标滚动
- 未连接：正文显示「等待 Mac 连接…」；断开时清空列表和 `!` 标记
- 提醒：响一声（走现有 `volume` 模块的音量设置）、整屏闪 3 次（参考噪音计的整屏闪），光标跳到触发的那一行。
  同一次列表里多个 session 同时触发，只响一次，光标跳到第一个

### 按键

- `;` `.` 上 / 下移（和 MUSIC 曲库页一致）
- 回车：发 `F <id>`，并清掉该行 `!`
- `` ` ``：返回菜单（断开 Mac）

### 菜单接入

按已有约定，新 app 插在 DIAG 前面：改 `menu_app.cpp` 的 `kItems`、`menu_app.h` 的 `App` 枚举、
`main.cpp` 的 `launch()`；数字直达键从 1–6 变成 1–7，菜单提示行和 README 同步更新。图标和主题色由实现决定。

## 留给实现的

- AGENTS 服务和特征的 UUID
- 中文字库的具体选择、行高、一屏行数
- 提示音的音高和时长、闪屏颜色
- agentbridge 的 LaunchAgent label 和日志轮转（不轮转也行）
- 菜单图标
