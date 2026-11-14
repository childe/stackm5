# CONFIG 菜单与统一标题行 设计

## 目的

两件相关的事：

1. 首页加一个 CONFIG 项，集中放「全局」设置：蓝牙设备管理、音量。现在音量散落在 MUSIC / DICE / 可视化页各自调，蓝牙状态只有进了 TV REMOTE / AGENTS 才看得到，换一台电视配对没有入口。
2. 所有页面的第 0 行（标题行）现在各写各的：每个 app 自己 `drawString` 左标题、自己算右对齐、自己拼 `v8`。统一成一个 `header` 模块：代码只写一处，展示格式一致，并在右端常驻蓝牙 / 音量 / 电量三个全局状态。

WiFi 不做（固件里没有任何用网络的功能）。

## 目标

- 菜单新增 CONFIG，插在 DIAG 前（DIAG 永远最后）。数字直达键从 1-7 变成 1-8：CONFIG=7，DIAG=8。
- CONFIG 页：
  - 音量：九档，`=` `-` 调，**改为断电保存**（NVS）。
  - 蓝牙：列出已绑定 / 已连接的设备，显示设备名（读不到时显示地址），标出谁正连着、是 TV 还是 Mac；可以忘记单个设备；可以「配对新设备」。
- 公共 `header` 模块，所有页面（含 VOCAB）第 0 行都由它画。
- 去掉各 app 标题行里自己画的 `vN`，以及菜单页的电量；这些由 header 右段统一显示。

## 非目标

- WiFi。
- 蓝牙「关闭」开关。NimBLE 只能初始化一次、不能反复 init/deinit（见 `src/ble.h`），本次不做关。
- 电量百分比曲线修正（线性公式在平台区高估，是另一件待办）。header 直接用 `M5.Power.getBatteryLevel()`。
- 去掉各 app 里的 `=` `-` 调音量快捷键。保留，CONFIG 只是多一个入口。

## 反目标（做到了也算失败）

- 已绑定的电视因为本次改动需要重新配对。HID 服务必须仍是第一个注册的服务，handle 不变。
- 进 CONFIG 页（或读设备名）导致已连着的 TV / Mac 掉线。
- header 挤掉页面原有的关键信息（可视化页的时间、NOISE 的报警线、编辑页的调号速度）—— 截断应先截标题，不截信息。
- 标题行闪烁或每帧重读电池 ADC 拖慢可视化页帧率。

## 约束

- 屏幕 240x135，`AsciiFont8x16` 8x16 等宽，一行 30 列。
- `src/ble.cpp` 是 TV REMOTE 和 AGENTS 共用的 NimBLE 栈：`ble::begin()` 幂等、只初始化一次；连接表在 NimBLE 回调线程改、loop 里读，用自旋锁；回调里不能做阻塞操作。
- Cardputer 在 BLE 里是外设（被连方）。设备名只能在连接建立后，作为 GATT client 反向读对方 GAP 服务的 Device Name 特征（0x1800 / 0x2A00）。
- 菜单项规则见 `menu_app.h`：`kItems` 表是唯一来源；新增 app 要改 `menu_app.cpp` 的 `kItems`、`App` 枚举、`main.cpp` 的 `launch()`、README。
- 能在 Mac 上测的纯逻辑放 `lib/` 并加 native 单元测试（同现有 `lib/carousel`、`lib/agents` 的做法）。

## 方案选择

蓝牙设备怎么标识（用户已选 B）：

- A. 只显示地址。连接回调直接拿得到，零额外代码；但用户认不出哪个地址是哪台设备。
- **B. 显示设备名，读不到退回地址。** 连接后在 loop 里用 GATT client 读对方 Device Name，按身份地址缓存进 NVS，未连接的已绑定设备也能显示名字。多一块需要真机调试的代码，但这是「显示配对的是什么」的本意。

标题行统一方式：

- A. 每个 app 保留自己的标题行，只在右端追加全局状态。改动小，但代码仍是十几处重复，格式仍各不相同 —— 不符合「代码与展示内容都统一」。
- **B. 公共 `header` 模块，页面只传字符串。** 所有页面都要改一处调用，但之后标题行只有一处实现。选 B。

## 设计

### 1. header 模块（`src/header.h/.cpp`，宽度计算放 `lib/header` 可测）

接口（示意，签名由实现者定）：

```cpp
namespace header {
// title：页面名（亮色，最左）；info：页面自己的信息（暗色，紧贴右段左侧，右对齐）
// bg：背景色（NOISE 报警闪屏时不是黑色）
void draw(LovyanGFX &g, const char *title, const char *info = "", uint16_t bg = TFT_BLACK);
// loop 每圈调一次：电量 / 蓝牙状态 / 音量变了就返回 true，调用方据此重绘
bool tick();
}
```

一行的布局（y 0..15）：

```
LIBRARY      12 songs  ᛒ ◀)8 ▮87
└ title ┘    └ info ┘  └ 状态段 ┘
```

- 状态段固定在最右，宽度固定（约 10 列），由 header 用基本图元画小图标：
  - 蓝牙：未启动 = 暗灰；广播中 = 闪烁（约 500ms 周期）；有连接 = 亮色。
  - 音量：喇叭图标 + 档位数字 `0`~`8`，静音时喇叭画叉。
  - 电量：电池外框 + 按百分比填充 + 两位数字（100 显示为 `99` 或满格，实现者定）。
- info 右对齐贴着状态段，title 从 x=0 开始；两者冲突时**先截 title**，info 整体保留；info 自身超过可用宽度才截 info。
- 电量每 5 秒读一次缓存，不在每帧读 ADC。
- 蓝牙闪烁需要周期重绘：`tick()` 在广播中每 500ms 返回一次 true。

各页面的 title / info：

| 页面 | title | info |
|---|---|---|
| 菜单 | STACKM5 | （空） |
| MUSIC 曲库 | LIBRARY / PLAYING | `12 songs` |
| MUSIC 编辑 | `01 *` / PLAYING | `1=C 4/4 120` |
| MUSIC 设置 | SETUP | `01` |
| 可视化 | `01 歌名` | `II 0:12/0:30` |
| VOCAB | VOCAB | （空） |
| TV REMOTE | TV REMOTE / PAIRING... | （空） |
| DICE | DICE | `sum 7` / `no IMU` |
| NOISE | NOISE / NOISE ALARM | `max 62 alarm 70` / `cal 3` |
| AGENTS | AGENTS | `Mac 3` / `no Mac` |
| CONFIG | CONFIG | （空） |
| DIAG | DIAG | （空） |

可视化页的配色（palette）只影响 title / info 的颜色时，允许 `draw` 多传颜色参数；由实现者决定是否需要。

### 2. 各页面的改动

- 删掉各自的标题行绘制代码和 `vN` 拼接，改调 `header::draw`。
- MUSIC 设置页底部的 `=- vol v8` 改成 `=- vol`（档位已在 header）。
- VOCAB 目前没有标题行，单词从 y=0 开始。改为正文整体下移到 y≥18，重排单词 / 音标 / 释义 / 例句的 y 坐标，保证最长单词（`kBigWordMaxChars`）和三行释义在 135 高内放得下；放不下时把释义行数从 3 减到 2。实现者在真机上确认。
- 主循环：每圈调 `header::tick()`，返回 true 时标记重绘当前页。

### 3. 音量持久化（`src/volume.cpp`）

- `volume::begin()` 从 NVS（`Preferences`，命名空间如 `cfg`，键 `vol`）读档位，没有则默认满档（保持现状）。
- `up()` / `down()` 改档后延迟写入（例如 1.5 秒无变化再写，同 MUSIC 自动存盘的理由：Flash 擦写寿命），或在离开 CONFIG / 各 app 时写入。实现者选一种，要求连按 8 次只产生一次写。

### 4. 蓝牙设备信息（`src/ble.cpp` 扩展）

新增能力：

- 记录每条连接的对端身份地址和角色（TV / Mac，Mac 的判断沿用现有 `claimMac`）。
- **读设备名**：`onConnect` 回调里只记「待读」标记；在 loop 侧（`ble::poll()` 之类，主循环每圈调）对待读的连接，通过服务器连接拿到 client（NimBLE-Arduino 2.x 的 `NimBLEServer::getClient(connInfo)` 或等价 API），读 0x1800/0x2A00。成功则按身份地址写入 NVS 名字缓存（命名空间如 `btnames`）。失败只记日志，显示地址。每条连接只尝试一次，读取不得断开连接。
- 列出设备：已绑定设备（`NimBLEDevice::getNumBonds()` / `getBondedAddress(i)`）并上当前连着但未绑定的连接；每项给出 名字或地址、是否连着、角色。
- 忘记单个设备：若连着先断开，`deleteBond(addr)`，删名字缓存。
- 配对新设备：断开所有连接、`deleteAllBonds()`、清名字缓存、`setRemoteActive()` 开始广播 HID。Mac 侧的 AGENTS 连接不依赖绑定，下次进 AGENTS 会照常连回。
- 给 header 的状态查询：`started()`、`advertising()`、`anyConnected()`。

### 5. CONFIG 页（`src/config_app.h/.cpp`）

进入时调 `ble::begin()`（幂等），这样蓝牙状态和配对动作都可用。注意：进 CONFIG 本身不调用 `setRemoteActive()`，不主动广播；只有「配对新设备」才开始广播。

```
CONFIG                 ᛒ ◀)8 ▮87
> volume          - 8 +
  TV  Living Room TV     ●
  Mac MacBook Pro        ●
      a4:c1:38:12:34:56
  + pair new device
;. move =- vol DEL forget `back
```

- `;` `.` 上下移动光标；`=` `-` 在任意行都调音量；`⏎` 在「pair new device」行执行配对新设备；`⌫` 在设备行忘记该设备；`` ` `` 回菜单。
- 忘记 / 配对新设备都是破坏性操作：按两次确认（同 MUSIC 删除曲子的「DEL again」做法），提示写在底行。
- 设备多于可视行数时滚动。名字超长截断。
- 配对新设备后，页面显示「广播中」状态，直到有设备连上。

### 6. 菜单

- `menu_app.cpp` 的 `kItems` 在 DIAG 前插入 CONFIG，配齿轮图标（基本图元画）；`App` 枚举加 `Config`；`main.cpp` 的 `launch()` 加分支；顶部注释和 README 的 1-7 改 1-8，README 的 app 表加一行 CONFIG。

## 留给实现者

- header 状态段图标的具体像素画法、各图标间距。
- 音量写 NVS 的时机（延迟写 / 离开时写），满足「连按只写一次」。
- NimBLE 读对端名的具体 API；若 2.x 没有现成的服务器侧 client，用 `NimBLEClient` 复用已有连接句柄的等价做法。
- VOCAB 重排的具体坐标。
- 哪些逻辑抽进 `lib/` 做 native 测试：至少 header 的 title/info 截断与宽度计算、设备列表的合并与排序。
