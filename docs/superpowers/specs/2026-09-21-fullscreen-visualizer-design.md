# 简谱播放器：全屏可视化（设计）

日期：2026-09-21
需求原文：音乐app中，list页点空格播放的时候，全屏可视化效果

## 1. 背景与目标

曲库页（Library）按 `SPC` 试听时，目前列表原地不动，只有标题变成绿色
"PLAYING"。本次给这条播放路径加一个**全屏可视化页**：播放开始即切入，
整屏画效果，四种风格可单键循环，另一键循环配色，支持暂停。

关键前提：播放器是我们自己合成的音（谱面 → 频率 → 喇叭），**不需要采集
音频**。每个音的频率、起止时间、时值、BPM 拍点都能确定性地算出来。
数据全部经 `Player` 一个口子出来（`Player::frame()` 只读快照 +
`score()` / `timeline()`，见第 5 节）——**Player 是唯一数据源，
Viz 不自己存谱面、不自己维护时间轴**。可视化由谱面数据驱动，
比伪 FFT 更准、更便宜。

不做（明确出范围）：

- 编辑器页 `ENTER` 播放维持原样，不进可视化（需求只提 list 页）
- 风格/配色不持久化到闪存，只在本次开机内记住（RAM 变量）
- 不采音频、不做 FFT
- 切换风格/配色时不显示风格名字（用户明确不要）
- **不做小节线 / 小节内拍号相关的任何显示**。`jianpu::Header` 只存
  `keyRoot` 和 `bpm`：头部行里的拍号 token 被 `parseHeaderLine` 显式
  丢弃（jianpu.cpp:57「拍号，忽略」），所以「每小节几拍」在当前数据模型里
  根本没有来源。要补就得扩 `Header` + 改解析器，而且不能简单拿分子当拍数
  ——本项目的「拍」是 `60000/bpm` 毫秒（四分音符），6/8 一小节是 3 拍不是
  6 拍，复合拍号要按分母换算。这属于 lib/jianpu 的独立改动，不塞进本次
  可视化。本次只用**一定拿得到的 `bpm`** 做拍点脉冲。

## 2. 交互

### 进入 / 退出

- 曲库页 `SPC` 播放**成功**（判据 = `playById` 返回 true，即 `start()` 之后
  `Player::isPlaying()` 为真，见第 5 节）→ 切到 `Page::Viz`。
  读盘失败 / 解析失败 / 空谱 / 零时长维持现状：留在曲库页，不切页。
- `` ` `` ：停止播放，回曲库页。
- 曲子自然放完（且不在暂停中）→ 自动回曲库页。

### 页内键位

| 键 | 作用 |
|---|---|
| `SPC` | 暂停 / 恢复 |
| `` ` `` | 停止并回列表 |
| `.` | 循环切换四种风格 |
| `,` | 循环切换四种配色 |

其余按键忽略。风格与配色的当前选择存 RAM，下次播放沿用。

### 屏幕布局（240×135，四种风格共用外框）

```
y 0..15   顶栏：左 = 曲号+首行预览（AsciiFont8x16，超长截断），
          右 = 已播/总时长 mm:ss/mm:ss；暂停时在时间左侧加 "II" 标记
y 18..127 效果区（各风格自由使用）
y 130..134 进度条：已播比例，faint 底 + bright 前景
```

## 3. 四种风格（全部由谱面数据驱动）

统一约定：**休止符（频率 ≤ 0）= 无激励**——频谱柱衰减回底噪、波形变平线、
卷帘留空、大字显示 `0`。暂停时画面冻结（用暂停那一刻的 elapsed 画）。

1. **频谱柱（Spectrum）**：24 根竖柱铺满效果区。当前音的音高映射到某根柱
   （半音数 → 柱下标，越界钳制），该柱及邻柱隆起成峰（邻柱按距离衰减），
   音符起始时刻峰值最高，随后在音符时值内指数衰减；所有柱有一条低的
   底噪线，避免静止时全黑。
2. **音高卷帘（Roll）**：音符按音高排成横向滚动的方块流（类钢琴卷帘）。
   横轴 = 时间窗口 [t−2s, t+4s]，纵轴 = 半音高度（按整首谱的音域
   min/max 半音数归一到效果区）。当前时刻是一条固定的竖线（约在左 1/3
   处），方块从右向左流过它；已播方块 dim、正在响的 bright、未播的 mid。
3. **示波器（Wave）**：一条横扫全屏的正弦波形。
   `y(x) = A · sin(2π · cycles · x/W + phase)`，三个量各管一件事：
   - `cycles = waveCyclesOnScreen(freq)`：**只有它随音高变**（高音密、
     低音疏，随 log2(f) 线性变化并钳制在可读范围）
   - `A = waveAmplitude(sinceOnsetMs, holdMs)`：音符时值内满幅衰减到约 40%
   - `phase = wavePhase(elapsedMs)`：**与频率无关**，按固定角速度随
     elapsed 匀速滚动

   相位不能写成 `2π·f·t`：换音时 `f` 跳变会让相位整体跳一大截，和「换音
   不跳变起点，只变密度」自相矛盾。固定角速度是唯一能同时满足「连续滚动」
   和「纯函数、可冻结」的写法。
4. **大字简谱（BigNote）**：效果区正中超大显示当前音的简谱写法
   （数字 + 高低八度圆点，AsciiFont8x16 放大数倍）；两侧淡色显示前一个
   / 后一个音符。拍点脉冲用**亮度呼吸**表达（拍首最亮、拍内衰减），
   不用字号缩放——位图字号只能整数倍，缩放会跳。亮度只由
   `beatPhase(elapsedMs, bpm)` 驱动，**不画小节拍点圆**（拍号无数据来源，
   见第 1 节「不做」）。

## 4. 配色

调色板 = 一套 4 级亮度（bright / mid / dim / faint 的 RGB565），四种风格
只从当前调色板取色，黑底：

| 名称 | 意象 |
|---|---|
| Gray（默认） | 白→灰，与背单词一致 |
| Cyan | 黑底青，示波器味 |
| Amber | 黑底橙黄，老终端味 |
| Green | 黑底绿，CRT 味 |

单色系不会触发之前「高饱和多色混排刺眼」的问题（实机反馈只针对多色混排）。

## 5. 实现结构

沿用项目既有模式（页面状态机 + 纯逻辑进 lib/ 可在 Mac 上单测）：

### lib/vizmodel/（新，纯逻辑，无 Arduino 依赖，允许依赖 lib/jianpu）

- `Palette` 结构与 `kPalettes[4]`
- 频谱：`int pitchToBar(float freq, int barCount)`（半音数映射+钳制）、
  `void barHeights(float freq, uint32_t sinceOnsetMs, uint32_t holdMs, float *out, int n)`
  （峰+邻柱衰减+时域包络+底噪，输出 0..1）
- 卷帘：
  - `SpanSemi scoreSemitoneSpan(const Score &)` → {minSemi, maxSemi}，
    **`begin()` 时算一次**存进 viz_app 的 RAM 状态，不每帧重扫全谱
  - `int rollBlocks(const Score &, const Timeline &, uint32_t elapsedMs,
    const RollGeom &, SpanSemi, RollBlock *out, int cap)` → 写入的方块数
    （≤ cap）。**调用方给固定容量数组、函数只填不分配**：每帧 30 次
    返回 `std::vector` 会在无 PSRAM 的 ESP32 上持续搅动堆。cap 按效果区
    宽度给（一屏最多画得下的方块数，约 64），越界的直接不填
  - 时间窗口只用来筛选和算 x：`RollBlock = {x0,x1,y,state}`
- 波形：`float waveCyclesOnScreen(float freq)`（log2 映射+钳制）、
  `float waveAmplitude(uint32_t sinceOnsetMs, uint32_t holdMs)`（满幅→40%）、
  `float wavePhase(uint32_t elapsedMs)`（固定角速度，**不吃 freq**，
  理由见第 3 节风格 3）
- 拍点：`float beatPhase(uint32_t elapsedMs, int bpm)` → [0,1)。
  只有这一个——`beatIndexInBar` 已删掉，它需要的 `beatsPerBar` 无数据来源
- 大字简谱：当前/前/后音符下标与简谱写法（数字、八度点数）的提取
- 播放时钟（纯算术，给 Player 复用，见下）：
  - `uint32_t resumeStartMs(uint32_t nowMs, uint32_t pausedElapsedMs)`
  - `uint32_t remainingHoldMs(uint32_t elapsedMs, uint32_t onsetMs, uint32_t holdMs)`
    → 剩余发声毫秒数；**落在 15% 静音间隔里或已过该音则返回 0**
    （0 的语义 = 恢复时不要补发这个音）

  放在 vizmodel 而不是再开一个 lib：就三个函数，且 vizmodel 已经是本
  feature 唯一的「纯逻辑、Mac 上可测」落点。

全部纯函数：给定同样输入必须输出同样结果（画面冻结、单测都靠这一点）。

### src/player.{h,cpp}（改）

#### 暂停状态机

状态由 `(_playing, _paused)` 两个 bool 表示，**合法状态只有三个**：

| 状态 | `_playing` | `_paused` | 含义 |
|---|---|---|---|
| Stopped | false | false | 没挂曲子 |
| Playing | true | false | 正常走时间轴 |
| Paused | true | true | 挂着曲子但时间冻结 |

`(false, true)` 是非法组合，下面的转移保证它永不出现。

| 转移 | 动作 |
|---|---|
| `start()` | **无条件 `_paused = false`**，`_startMs = millis()`，`_index = -1`，`_playing = true`，然后 `update()` |
| `stop()` | **无条件 `_paused = false`**，`_playing = false`，`_index = -1`，`Speaker.stop()` |
| `pause()` | 仅在 Playing 下生效（Paused/Stopped 时是 no-op）。`_pausedElapsed = elapsedMs()`；`Speaker.stop()`；`_paused = true` |
| `resume()` | 仅在 Paused 下生效（其余 no-op）。`_startMs = resumeStartMs(millis(), _pausedElapsed)`；`_paused = false`；再按下面补音 |
| 自然放完 | `update()` 里 `indexAt` 返回 -1 → 走 `stop()`，于是 `_paused` 也被清掉 |

**`start()` / `stop()` 必须清 `_paused` 是硬要求，不是可选项**：漏了的话
「暂停 → `` ` `` 停止 → 重新播另一首」会带着残留的 `_paused` 进入新播放，
`update()` 一进来就 return，结果是喇叭全哑、画面冻死在第一帧，而
`isPlaying()` 还是 true，看起来像死机。

不变量（每次转移后都成立）：

- `!_playing → !_paused`
- Paused 期间 `elapsedMs()` 恒等于 `_pausedElapsed`（多次调用不漂移）
- Paused 期间 `currentIndex()` 按 `_pausedElapsed` 算，恒定
- Paused 期间 `isPlaying()` 保持 true（曲子还挂着，主循环不会误判成放完）、
  `update()` 直接返回（不推进 `_index`、不碰喇叭）

#### 恢复时的补音

`resume()` 用 `remainingHoldMs(_pausedElapsed, onset[_index], hold[_index])`：

- 返回 > 0 且该音 `freq > 0` → `Speaker.tone(freq, 剩余毫秒)`。不补的话恢复后
  半个音是哑的，听起来像丢一拍
- 返回 0（暂停点落在那 15% 静音间隔里、或已过该音）→ **不发声**，直接让
  `update()` 在下一个音的 onset 接管
- 该音是休止符（`freq <= 0`）→ 不发声

补音后不动 `_index`：`update()` 下一轮算出的 `idx` 仍等于 `_index`，会在
「还在同一个音里」那一支提前 return，不会重复触发。

#### 只读播放快照（可视化的唯一数据源）

Player 已经**按值**持有 `_score` 和 `_timeline`（player.h:38-39，现状如此），
生命周期覆盖整个播放过程。把它们暴露成只读，Viz 就不需要自己再存一份：

```cpp
struct PlaybackFrame {
    bool     playing;
    bool     paused;
    uint32_t elapsedMs;   // paused 时为冻结值
    uint32_t totalMs;
    int      index;       // -1 = 没在播
    uint32_t onsetMs;     // index >= 0 时有效
    uint32_t holdMs;
    float    freq;        // <= 0 表示休止符
};

PlaybackFrame frame() const;                    // 一次取齐，同一 elapsed
const jianpu::Score    &score() const;          // 卷帘要扫全谱
const jianpu::Timeline &timeline() const;
```

`frame()` 一次性算齐所有量，避免 Viz 分别调 `elapsedMs()` /
`currentIndex()` 时跨 `millis()` 边界拿到不自洽的组合（一帧里 index 已经
是下一个音、elapsed 还是上一个音的）。**Viz 不得自己维护第二条时间轴。**

### src/viz_app.{h,cpp}（新）

```cpp
void begin(const char *title, uint8_t id, const Player &);
void draw(LovyanGFX &, const Player &);
bool handleKey(char, Player &);   // false = 要回列表
```

**不收 `Score` 参数**：`playById` 里的 `jianpu::Score s` 是局部变量，
`gPlayer.start(s)` 之后它就出作用域了。Viz 要谱面时走
`player.score()` / `player.timeline()`（Player 按值持有，见上），
绝不持有指向调用方局部量的引用或指针。

标题同理**必须拷进自己的固定缓冲**（`char _title[48]`，`strlcpy` 截断）：
调用方给的是 `gEntries[gSel].preview.c_str()`，而 `refreshEntries()` 会
整个重建 `gEntries` 这个 `std::vector<Entry>`，`std::string` 的缓冲连带
失效——存下这个 `const char *` 就是悬垂指针。

`begin()` 里另外算一次并存下：`scoreSemitoneSpan(player.score())`（卷帘
音域，每帧重扫全谱是浪费）。id 只用来显示曲号。

风格/配色枚举与「本次开机记住」的 RAM 状态封在 .cpp 内；四种风格各一个
draw 函数，从 vizmodel 拿数据、往画布画像素。卷帘的方块缓冲是 .cpp 内的
`static RollBlock buf[64]`（固定容量，不每帧分配）。

### src/main.cpp（改，接线）

- `Page` 枚举加 `Viz`
- **`playById` 改成返回 `bool`**（现在是 `void`，调用方没法知道成没成）。
  返回值取 `start()` 之后的 `gPlayer.isPlaying()`，而不是只看解析守卫：
  `library::load` 失败要 false、`s.error.ok && !s.notes.empty()` 不过要
  false，而且 `Player::start()` 自己还会在 `totalMs == 0` 时把 `_playing`
  置回 false（player.cpp:10-13）——只看解析守卫会漏掉最后这种，切进 Viz
  面对一个没在播的 Player
- 只有 `playById` 返回 true 才 `viz_app::begin(...)` 并切 `Page::Viz`；
  false 就留在曲库页，行为与现在完全一致
- 主循环：Viz 页每 ~33ms 置脏重绘（约 30fps；其余页维持现状）；
  检测「不在播放且不在暂停」→ 自动回曲库页并刷新列表
- 自动存盘守卫维持只在 Editor/Settings 生效（Viz 不碰谱面缓冲）

## 6. 错误与边界

- 读盘失败 / 解析失败 / 空谱 / `totalMs == 0`：`playById` 返回 false，
  不进 Viz，行为与现在一致
- 全是休止符的谱：四种风格都有定义（见第 3 节统一约定），不崩不黑屏
- 单音符/极短曲：进度条与自动退出照常
- 音域只有一个半音（卷帘归一化分母为 0）：固定画在效果区中线
- BPM 极端值（20~300，settings 已有范围）：拍点相位用 uint32 毫秒算，
  不溢出

## 7. 测试策略

- **native 单测（lib/vizmodel 全覆盖）**：音高→柱下标的边界与钳制、
  包络在时值内单调不增且不为负、卷帘坐标（过去/当前/未来分类、滚动方向、
  音域归一化含单半音退化、**方块数超过 cap 时只填 cap 个且不越界写**）、
  波形周期数钳制与振幅范围、**`wavePhase` 不吃 freq（换音相位连续）**、
  拍点相位回绕、调色板 4 套且互不相同
- **播放时钟纯算术（必测，不是「若可抽离」）**：`resumeStartMs` /
  `remainingHoldMs` 已经是不依赖 `millis()` 的纯函数，`now` 从参数进来，
  所以这几条必须有 native 单测，一条都不能缺：
  - 暂停在发声段中途 → `remainingHoldMs` = onset+hold−elapsed，且 0 < 它 ≤ hold
  - 暂停恰好落在 15% 静音间隔里（onset+hold ≤ elapsed < onset+dur）→ 返回 0
  - 暂停在音符边界（elapsed == onset、elapsed == onset+hold）→ 分别为
    hold 和 0，无 off-by-one
  - `resumeStartMs` 使 `now − _startMs` 恰好等于 `_pausedElapsed`（冻结值
    不漂移），并覆盖 `now` 小于 `pausedElapsed` 的无符号回绕情形
- **Player 状态机**：`(_playing, _paused)` 的转移表逐条验证
  `start`/`stop`/`pause`/`resume` 后的状态，重点是「`start()` 和 `stop()`
  清掉 `_paused`」和「`(false, true)` 永不出现」。Player 直接 include
  `M5Cardputer.h`，native 环境链不上，所以这层不做 Speaker 的接口注入
  （为四个 bool 转移引一层虚接口不划算）——**转移表和补音时机上机逐条走查**，
  纯算术部分已被上一条单测兜住
- **上机目检**：四风格 × 四配色切换、暂停/恢复补音、暂停中按 `` ` ``
  停止后立刻重播另一首（验证 `_paused` 没残留）、自然放完自动退出、
  30fps 下无闪烁（离屏画布）

## 8. 键位与现有页面的冲突检查

Viz 页独占按键处理（进入后其它页的 handler 不再收键），`SPC/`.`/`,`/`` ` ``
只在 Viz 页内解释，与曲库页的 `SPC 播放`、编辑器的输入互不影响。
