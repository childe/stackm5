# 简谱播放器：全屏可视化（设计）

日期：2026-09-21
需求原文：音乐app中，list页点空格播放的时候，全屏可视化效果

## 1. 背景与目标

曲库页（Library）按 `SPC` 试听时，目前列表原地不动，只有标题变成绿色
"PLAYING"。本次给这条播放路径加一个**全屏可视化页**：播放开始即切入，
整屏画效果，四种风格可单键循环，另一键循环配色，支持暂停。

关键前提：播放器是我们自己合成的音（谱面 → 频率 → 喇叭），**不需要采集
音频**。每个音的频率、起止时间、时值、BPM 拍点都能从
`jianpu::Score` + `jianpu::Timeline` + `Player`（`currentIndex()` /
`elapsedMs()` / `totalMs()`）确定性地算出来。可视化由谱面数据驱动，
比伪 FFT 更准、更便宜。

不做（明确出范围）：

- 编辑器页 `ENTER` 播放维持原样，不进可视化（需求只提 list 页）
- 风格/配色不持久化到闪存，只在本次开机内记住（RAM 变量）
- 不采音频、不做 FFT
- 切换风格/配色时不显示风格名字（用户明确不要）

## 2. 交互

### 进入 / 退出

- 曲库页 `SPC` 播放**成功**（解析通过且有音符，即现有 `playById` 的守卫
  通过）→ 切到 `Page::Viz`。解析失败/空谱维持现状：留在曲库页。
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
3. **示波器（Wave）**：一条横扫全屏的正弦波形。频率决定屏幕上的周期数
   （高音密、低音疏，周期数随 log2(f) 线性变化并钳制在可读范围），
   振幅在音符时值内从满幅衰减到约 40%，相位随 elapsed 连续滚动
   （换音不跳变起点，只变密度）。
4. **大字简谱（BigNote）**：效果区正中超大显示当前音的简谱写法
   （数字 + 高低八度圆点，AsciiFont8x16 放大数倍）；两侧淡色显示前一个
   / 后一个音符。拍点脉冲用**亮度呼吸**表达（拍首最亮、拍内衰减），
   不用字号缩放——位图字号只能整数倍，缩放会跳。底部一排拍点圆
   （每小节的拍数个），当前拍实心。

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
- 卷帘：`RollBlock` 列表计算——输入 Timeline/Score、elapsedMs、窗口参数、
  效果区几何，输出各方块的 {x0,x1,y,state}；音域 min/max 半音的归一化
- 波形：`float waveCyclesOnScreen(float freq)`（log2 映射+钳制）、
  `float waveAmplitude(uint32_t sinceOnsetMs, uint32_t holdMs)`（满幅→40%）、
  相位滚动 `float wavePhase(uint32_t elapsedMs, float freq)`
- 拍点：`float beatPhase(uint32_t elapsedMs, int bpm)` → [0,1)、
  `int beatIndexInBar(uint32_t elapsedMs, int bpm, int beatsPerBar)`
- 大字简谱：当前/前/后音符下标与简谱写法（数字、八度点数）的提取

全部纯函数：给定同样输入必须输出同样结果（画面冻结、单测都靠这一点）。

### src/player.{h,cpp}（改）

加 `pause()` / `resume()` / `isPaused()`：

- `pause()`：仅在播放中生效。记录 `_pausedElapsed = elapsedMs()`，喇叭停声
  （`Speaker.stop()`），置 `_paused`
- `resume()`：`_startMs = millis() - _pausedElapsed`，清 `_paused`；若恢复
  时刻仍落在当前音的发声窗口内（elapsed < onset+hold），把**这个音剩余的
  时长补发出来**（不补的话恢复后半个音是哑的，听起来像丢一拍）
- 暂停期间：`isPlaying()` 保持 true（曲子还挂着）、`update()` 直接返回、
  `elapsedMs()` 返回冻结值、`currentIndex()` 按冻结值算

### src/viz_app.{h,cpp}（新）

`begin(const jianpu::Score &, const char *title, uint8_t id)` /
`draw(LovyanGFX &, Player &)` / `handleKey(char, Player &) → bool`
（false = 要回列表）。风格/配色枚举与「本次开机记住」的 RAM 状态封在
.cpp 内；四种风格各一个 draw 函数，从 vizmodel 拿数据、往画布画像素。

### src/main.cpp（改，接线）

- `Page` 枚举加 `Viz`
- `playById` 成功后 `viz_app::begin(...)` 并切 `Page::Viz`
- 主循环：Viz 页每 ~33ms 置脏重绘（约 30fps；其余页维持现状）；
  检测「不在播放且不在暂停」→ 自动回曲库页并刷新列表
- 自动存盘守卫维持只在 Editor/Settings 生效（Viz 不碰谱面缓冲）

## 6. 错误与边界

- 解析失败/空谱：不进 Viz（现有 `playById` 守卫），行为与现在一致
- 全是休止符的谱：四种风格都有定义（见第 3 节统一约定），不崩不黑屏
- 单音符/极短曲：进度条与自动退出照常
- 音域只有一个半音（卷帘归一化分母为 0）：固定画在效果区中线
- BPM 极端值（20~300，settings 已有范围）：拍点相位用 uint32 毫秒算，
  不溢出

## 7. 测试策略

- **native 单测（lib/vizmodel 全覆盖）**：音高→柱下标的边界与钳制、
  包络在时值内单调不增且不为负、卷帘窗口坐标（过去/当前/未来分类、
  滚动方向、音域归一化含单半音退化）、波形周期数钳制与振幅范围、
  拍点相位回绕、调色板 4 套且互不相同
- **Player 暂停逻辑**：elapsed 冻结/恢复的纯算术若可抽离则抽到可测处；
  依赖 millis 的部分上机验证
- **上机目检**：四风格 × 四配色切换、暂停/恢复补音、自然放完自动退出、
  30fps 下无闪烁（离屏画布）

## 8. 键位与现有页面的冲突检查

Viz 页独占按键处理（进入后其它页的 handler 不再收键），`SPC/`.`/`,`/`` ` ``
只在 Viz 页内解释，与曲库页的 `SPC 播放`、编辑器的输入互不影响。
