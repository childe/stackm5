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
  - 蓝牙：列出已绑定 / 已连接的设备，显示设备名（读不到时显示地址），标出谁正连着、角色（TV / Mac / 未知）；可以忘记单个设备；可以「配对新设备」（保留已有绑定）。
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
// 只管标题行自己的三个状态；页面正文（如 CONFIG 的设备列表）的变化不归它管，见第 4、5 节的 revision
bool tick();
}
```

一行的布局（y 0..15）：

```
LIBRARY      12 songs  ᛒ ◀)8 ▮87
└ title ┘    └ info ┘  └ 状态段 ┘
```

- 状态段固定在最右，占 x=168..239（72px，9 列），由 header 用基本图元画小图标。像素分配固定如下（x 为闭区间）：

  | x | 内容 |
  |---|---|
  | 168-171 | 空隙（与 info 隔开） |
  | 172-179 | 蓝牙图标 8px |
  | 180-181 | 空隙 |
  | 182-189 | 喇叭图标 8px |
  | 190-197 | 音量档位 1 位数字 |
  | 198-203 | 空隙 |
  | 204-219 | 电池图标 16px（外框 14 + 正极 2） |
  | 220-223 | 空隙 |
  | 224-239 | 电量 2 位数字 |

  图标含义：
  - 蓝牙：未启动 = 暗灰；广播中 = 闪烁（约 500ms 周期）；有连接 = 亮色。
  - 音量：喇叭图标 + 档位数字 `0`~`8`，静音时喇叭画叉。
  - 电量：电池外框 + 按百分比填充 + 两位数字；100 显示为 `99`（数字恒为 2 位，0-9 补前导空格）。
- 文字区 x=0..167，共 21 列。info 右对齐，末字符结束于 x=167；title 从 x=0 开始。title 与 info 之间至少 1 列空格。
- 截断规则（`lib/header` 纯函数实现，native 测试直接用下面的样例做输入）：
  1. 若 `len(info) > 21`：info 保留前 20 个字符 + `~`，title 不显示。
  2. 否则 title 可用列数 `n = 21 - len(info) - (info 非空 ? 1 : 0)`。若 `len(title) <= n`，原样显示；若 `n >= 2`，显示 title 前 `n-1` 个字符 + `~`；若 `n < 2`，title 不显示。
  3. info 永不为 title 让位。
- 最长实际信息的布局样例（`|` 为文字区左右边界，共 21 列）：

  | 页面 | title | info | 结果 |
  |---|---|---|---|
  | NOISE 报警 | NOISE ALARM | `max 62 alarm 70`（15） | `\|NOIS~ max 62 alarm 70\|` |
  | 可视化暂停 | `01 Twinkle Star` | `II 12:34/12:34`（14） | `\|01 Tw~ II 12:34/12:34\|` |
  | MUSIC 编辑 | PLAYING | `1=C 4/4 120`（11） | `\|PLAYING   1=C 4/4 120\|` |
  | AGENTS | AGENTS | `no Mac`（6） | `\|AGENTS         no Mac\|` |

  NOISE 报警时 title 被截成 `NOIS~` 可接受：报警状态由整屏背景色表达，报警线数值完整保留。
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
- 主循环在 CONFIG 页时还调 `config_app::tick()`：它比较 `ble::devicesRevision()` 与上次绘制时的值，不同就返回 true 标记重绘。header 只管标题行三个状态，设备列表的变化（名字读到、角色确认、绑定完成等）由这条路径负责。

### 3. 音量持久化（`src/volume.cpp`）

- `volume::begin()` 从 NVS（`Preferences`，命名空间如 `cfg`，键 `vol`）读档位，没有则默认满档（保持现状）。
- `up()` / `down()` 改档后延迟写入（例如 1.5 秒无变化再写，同 MUSIC 自动存盘的理由：Flash 擦写寿命），或在离开 CONFIG / 各 app 时写入。实现者选一种，要求连按 8 次只产生一次写。

### 4. 蓝牙设备信息（`src/ble.cpp` 扩展）

#### 4.1 数据模型：持久设备记录 与 当前连接记录分开

- **持久设备记录**（NVS 命名空间如 `btdev`，按身份地址作键）：身份地址、地址类型、名字（可空）、已确认角色（TV / Mac / 未知）。只在「确认到」时写：读到名字时写名字；角色确认时写角色。
- **当前连接记录**（RAM，沿用现有固定数组 + 自旋锁）：连接句柄、对端身份地址、连接代次（`gen`，每次 `onConnect` 全局自增分配）、本次连接上观察到的角色（未知 / TV / Mac）、读名状态（待读 / 读取中 / 完成 / 失败）。
- 角色判定：
  - Mac：连接写过或订阅过 AGENTS 特征（沿用现有 `claimMac`）。
  - TV：连接订阅了 HID 输入报告特征（在 `remote_app` 的 HID 特征 `onSubscribe` 回调里调 `ble::claimTv(connHandle)`）。
  - 两者都没有发生：未知。「没被 `claimMac` 认领」**不**推出是 TV。
  - 连接上的角色一经确认，写入该设备的持久记录；断开后列表显示持久记录里的角色。
- 设备列表 = 持久记录与 `NimBLEDevice::getBondedAddress(i)` 的并集，再并上当前连着但未绑定、也无持久记录的连接。每项：名字（无则地址）、是否连着（看当前连接记录里有没有该身份地址）、角色（连着时优先用本次连接观察到的角色，否则用持久记录）。合并与排序逻辑放 `lib/` 做 native 测试。
- **revision**：`ble` 维护一个原子计数 `ble::devicesRevision()`。连接、断开、绑定完成、名字读到、角色确认、忘记设备、配对模式进入 / 退出，都把它加 1。CONFIG 页据此重绘（见第 5 节）。

#### 4.2 读设备名：专用任务，不阻塞主循环

NimBLE-Arduino 2.x 的同步 `readValue()` 会等待 GATT 操作完成，放在主循环里会让键盘处理、`Player::update()` 和绘制一起停顿（进过 TV REMOTE 后 `gRemoteActive` 一直为真，返回菜单播放音乐时电视仍会回连，所以这条路径真实存在）。因此：

- `onConnect` 回调只把 `{connHandle, gen}` 放进一个 FreeRTOS 队列（回调运行在 NimBLE 主机任务里，用 `xQueueSend` 且等待时间为 0；队列满就丢弃，该连接记为「失败」）。
- 一个专用低优先级任务（`ble_name`，栈约 4KB，`ble::begin()` 里创建）串行从队列取任务：
  1. 先在自旋锁下检查该句柄当前的 `gen` 是否与任务里的一致；不一致（已断开或句柄被新连接复用）就丢弃。
  2. 通过服务器连接拿 client（`NimBLEServer::getClient(connInfo)` 或复用连接句柄的等价做法），发现 0x1800 服务、读 0x2A00。整个读取总超时 3 秒：优先用 NimBLE 读操作自带的超时参数；API 不提供时由任务侧计时，超时即放弃结果。
  3. 拿到结果后再检查一次 `gen`；仍一致才把名字写进持久记录（NVS 写也在这个任务里做），并 `revision++`。
  4. 失败 / 超时 / 断连：只打日志，该连接标为「失败」，列表继续显示地址。每条连接只尝试一次。
- 读取不得断开连接：任务里不调用 `disconnect()`，client 对象用完只释放本地对象、不断链路。
- 主循环（`ble::poll()` 若存在）不做任何 GATT 操作，只读快照。

#### 4.3 忘记单个设备

若连着先断开，`deleteBond(addr)`，删持久记录，`revision++`。

#### 4.4 配对新设备：保留已有绑定

不再 `deleteAllBonds()`：已绑定的电视不因「配对新设备」而失去绑定（与反目标第一条一致）。想换掉旧电视，用户另行「忘记」它。

- 进入**配对模式**：`ble::startPairing()` 置 `gPairing = true`，记下当前已绑定地址集合，`revision++`，并 `refreshAdvertising()`。
- 配对模式下的广播策略：`needRemote = gRemoteActive && !remoteConnected()` 改为 `needRemote = gPairing || (gRemoteActive && !remoteConnected())`。即旧电视在配对期间回连也**不**停止 HID 广播，新设备仍能发现并连接。这要求同时连接数 ≥ 3（旧电视 + Mac + 新设备）；确认 `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` ≥ 3，不足时在 `platformio.ini` 调到 3，`kMaxConns` 保持 4 不变。
- 配对成功：某条连接完成鉴权（`onAuthenticationComplete`，`isBonded()` 为真），且其身份地址不在进入时记下的集合里 → 退出配对模式，`gRemoteActive = true`（之后按常规规则回连广播），`revision++`。
- 取消：用户在 CONFIG 页再按一次 `⏎`（底行提示 `ENT cancel`），或离开 CONFIG 页 → 退出配对模式。
- 超时：进入后 120 秒没有新绑定 → 自动退出配对模式。
- 退出配对模式（成功 / 取消 / 超时）后统一调 `refreshAdvertising()`，回到常规规则：遥控器没连着（且 `gRemoteActive`）才广播 HID，AGENTS 按原规则。
- 旧电视和新设备同时连着时，HID 按键报告（`notify` 不带句柄）会发给所有订阅者，两台都会收到按键。这是保留绑定的自然结果，不额外处理。
- Mac 侧的 AGENTS 连接不受配对模式影响。

#### 4.5 给 header 的状态查询

`started()`、`advertising()`、`anyConnected()`；另给 CONFIG 用 `pairing()`、`devicesRevision()`、设备列表快照。

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

- `;` `.` 上下移动光标；`=` `-` 在任意行都调音量；`⏎` 在「pair new device」行进入配对模式，配对模式下再按 `⏎` 取消；`⌫` 在设备行忘记该设备；`` ` `` 回菜单（同时退出配对模式）。
- 忘记设备是破坏性操作：按两次确认（同 MUSIC 删除曲子的「DEL again」做法），提示写在底行。配对新设备不再删除绑定，不需要二次确认。
- 重绘：`config_app::tick()` 发现 `ble::devicesRevision()` 变化就重绘（见第 2 节）。重建列表后光标按设备身份地址找回原来那一项；原项已消失（被忘记）时光标停在同一序号，越界则夹到末项。
- 设备多于可视行数时滚动。名字超长截断。
- 配对模式期间，「pair new device」行显示 `pairing... 87s`（剩余秒数，每秒重绘一次），直到配对成功、取消或超时。

### 6. 菜单

- `menu_app.cpp` 的 `kItems` 在 DIAG 前插入 CONFIG，配齿轮图标（基本图元画）；`App` 枚举加 `Config`；`main.cpp` 的 `launch()` 加分支；顶部注释和 README 的 1-7 改 1-8，README 的 app 表加一行 CONFIG。

## 留给实现者

- header 状态段各图标在给定像素框内的具体画法。
- 音量写 NVS 的时机（延迟写 / 离开时写），满足「连按只写一次」。
- NimBLE 读对端名的具体 API；若 2.x 没有现成的服务器侧 client，用 `NimBLEClient` 复用已有连接句柄的等价做法。约束不变：在 `ble_name` 任务里执行、3 秒超时、`gen` 校验、不断开连接。
- VOCAB 重排的具体坐标。
- 哪些逻辑抽进 `lib/` 做 native 测试：至少 header 的 title/info 截断（用第 1 节样例表）、设备列表的合并与排序、角色合并规则（本次连接观察 > 持久记录 > 未知）、配对模式的成功判定（新地址不在进入时集合中）。
