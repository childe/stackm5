# 首页横向图标轮播菜单 — Design

## 需求

首页从「6 行文字 + 数字键」改成横向排列的「图标 + 文字」列表：

- 一屏显示 3 个 item
- 左右键让 item 旋转（轮播）
- 空格或回车进入当前 app

## 交互

- **轮播，不是翻页**：6 个 item 首尾相接排成一圈。屏幕中间那个是「当前项」，左右各露一个邻居。
  左右键每按一次转一格；从 DIAG 再往右转回到 MUSIC。
  （「每页 3 个、左右翻页」只有 6/3 = 2 页，每页里还得再有一个光标，和「旋转」对不上，所以不选。）
- **按键**：
  - `,` 左转（上一个），`/` 右转（下一个）。Cardputer 没有独立方向键，这两个键帽上印着 ← →，
    遥控器 app（`lib/remotemap`）用的也是这对键
  - 空格 / 回车：进入**当前项**，也就是 `selected()`，即使动画还没播完
  - 数字 `1`–`6` 保留，直接进入对应 app（兼容老习惯，不占别的键）；同时把当前项设成那一项、
    停掉动画，这样返回首页时它正好停在中间
- **动画**：转一格时 item 横向滑动 `kStepMs = 160`ms（ease-out 三次方），中间项放大、两侧缩小变暗，
  大小和亮度跟着位置连续变化。动画中再按键不会被丢掉：`selected` 立刻加减 1，视觉偏移 offset
  从当前画面位置再加 ±1，然后钳到 `±kMaxLag = 1.5` 格，免得连按时动画拖尾
- **指示点**跟 `selected()` 走，按下就跳，不跟着画面滑
- **记住位置**：carousel 状态放在 `menu_app` 的静态变量里，进 app 不清零。不写 NVS。
- **开机第一屏是 MUSIC / VOCAB / TV REMOTE**：MUSIC 在最左，当前项（中间）是 VOCAB。
  （初版开机当前项是 MUSIC，左边露出的是绕回来的 DIAG，实机看很怪 —— 2026-09-29 用户反馈。）
  因为开机当前项是 VOCAB，提示行得是 `SPC flip ENT skip ,/ move`，否则第一屏看不到怎么转
  offset 完全由 `millis()` 算出来，所以动画中途进 app、过一会儿再回来，看到的是已经停稳的画面
- **DIAG 永远排最后**（已有约定）

## 画面（240×135，AsciiFont8x16，一个字 8×16）

```
STACKM5                 87% 4.01V        y 0–15   标题行，和现在一样
            ┌─────────┐                  y 20     描边框顶（x 82–158，w 76，h 76，圆角 6）
    ♪♪      │  ▭▭▭    │     ▭═           y 46     图标中心（中间 40px，两侧 28px）
            │         │
   MUSIC    │  VOCAB  │   TV REMOTE      y 74–89  标签（在框内，居中于 item 的 x）
            └─────────┘                  y 95     描边框底
             ● ○ ○ ○ ○ ○                 y 104    指示点（x 95,105…145，半径 2）
 ,/ move SPC/ENT open 1-6               y 119–134 提示行（y = kScreenH − kCharH = 119 贴底；和现在菜单页
                                                  一样，不是其它页用的 kHintY = 112）
```

- 三个槽的中心 x = 40 / 120 / 200（`x = 120 + 80·pos`）。最长标签 "TV REMOTE" 是 72px：
  在两侧时占 4–76 / 164–236，不出屏；在中间时占 84–156，在描边框（82–158）里面
- 按格位 `pos`（小数）插值：`|pos| ≤ 1` 时图标尺寸 `40 − 12·|pos|`、亮度 `1 − 0.55·|pos|`；
  `|pos| > 1` 时保持 28px / 0.45。`|pos| ≥ 2`（中心 x 出了屏幕 ±40）就不画
- 描边框固定在屏幕中间，item 从框里滑进滑出；框用 DARKGREY
- 主题色（RGB565）：MUSIC `TFT_CYAN`、VOCAB `TFT_YELLOW`、TV REMOTE `TFT_GREEN`、DICE `TFT_ORANGE`、
  NOISE `TFT_MAGENTA`、DIAG `TFT_LIGHTGREY`。图标和标签都用 `dim565(主题色, 亮度)`
- 提示行：默认 `,/ move SPC/ENT open 1-6`（24 字 = 192px；末尾的 `1-6` 提醒数字键还能直接进，
  否则老菜单上一眼可见的数字键在新画面里就看不到了）；当前项是 VOCAB 时换成 `SPC flip ENT skip ,/ move`（25 字 200px，背单词的两个键加上转动键）
  （背单词页排满了放不下提示，原来就写在首页上，这个习惯保留）
- 电量右上角，每 2 秒刷新一次，保持不变

### 图标

全部用 LovyanGFX 的基本图元现场画，参数是 `(g, cx, cy, size, color)`，可以任意缩放，不带位图：

| app | 图标 |
|---|---|
| MUSIC | 两个连着横梁的八分音符 |
| VOCAB | 摊开的书（两页 + 几行字） |
| TV REMOTE | 带底座的电视屏幕 |
| DICE | 圆角方块 + 五点 |
| NOISE | 分贝表盘：上半圆弧 + 5 道刻度 + 指向右上（偏响）的指针 + 圆心。初版的 5 根音量柱实机不好认、像信号格，2026-09-29 按用户反馈换掉 |
| DIAG | 圆角框里一条心电图折线 |

## 代码结构

- **`lib/carousel/`（新，纯逻辑，零硬件依赖，能跑单元测试）**
  - `int wrap(int i, int n)`：取模，负数也返回 `[0, n)`
  - `class Carousel`，构造函数 `explicit Carousel(int count, int initial = 0)`（initial 取模后作为开机当前项）：`selected()`、`step(int dir, uint32_t nowMs)`、`jump(int index)`
    （设当前项并停掉动画）、`float offset(uint32_t nowMs)`、`bool animating(uint32_t nowMs)`。
    `step(+1)` 后 offset 从 +1 缓动到 0；动画中再按键，从当前 offset 再加 ±1，然后钳到 ±1.5
  - `float slotPos(int item, int sel, float offset, int n)`：item 在屏幕上的格位（0 = 正中，负数在左）。
    候选差值 `d0 = wrap(item − sel, n)` 和 `d0 − n`，取 `|d + offset|` 更小的那个；相等时取非负的 d0。
    **函数返回 `d + offset`**（d 是选中的那个候选差值），也就是画面上的实际格位。
    所以 sel=5 时 item 0 在 +1（右边），sel=0 时 item 5 在 −1（左边），n=6 距离 3 时算 +3；
    刚 `step(+1)` 完（offset = +1）时旧当前项是 d = −1、返回 0，仍画在正中，然后才滑走
  - `SlotGeom slotGeom(float pos)` → `{int x; int size; float bright; bool visible;}`，规则见上面
  - `uint16_t dim565(uint16_t c, float k)`：R/G/B 分别乘 k 后四舍五入；`k ≤ 0` 返回 0，`k ≥ 1` 原样返回 c
- **`src/menu_app.{h,cpp}`（新）**：
  - `enum class App { None, Music, Vocab, Remote, Dice, Noise, Diag };`
  - item 表（标签、主题色、图标函数、App）按显示顺序排，DIAG 放最后；这张表是菜单项唯一的来源
  - `void draw(LovyanGFX &g)`、`bool tick()`（动画进行中返回 true，要求重绘）
  - `App handleKey(char c)`：和其他 app 一样只收 char。`,` `/` 转动；`' '` 或 `'\n'` 返回当前项；
    `'1'`–`'6'` 先 `jump` 再返回那一项；其它键返回 `App::None`
- **`src/main.cpp`**：
  - 删掉 `drawMenu`，`draw()` 的 Menu 分支调 `menu_app::draw(g)`
  - `handleMenuKeys`：先处理 `st.enter`（翻译成 `'\n'`），再遍历 `st.word`。**空格只从 `st.word` 里的 `' '` 拿，
    不查 `st.space`**（空格两边都会出现，`handleEditorKeys` 踩过这个坑）。拿到非 None 的 App 就调
    `launch(app)` 并立即 return，同一批按键剩下的字符不再处理
  - 新增 `static void launch(menu_app::App a)`：`switch` 调原来的启动代码（Library 要 `refreshEntries()`，
    其余调 `*_app::begin()`），设 `gPage`，置脏。各 app 的返回逻辑（包括 `noise_app::end()`）不动
  - `loop()`：只在 `gPage == Page::Menu` 时调 `menu_app::tick()`，返回 true 就置脏（约 16ms 一次，
    `tick` 内部限频）；每 2 秒刷新一次电量的逻辑保留
  - 顶部注释的「菜单页」段改为：
    ```
     * 菜单页（横向轮播，一屏 3 个，中间的是当前项）
     *   ,  /        左转 / 右转
     *   空格 或 ⏎    进入当前项
     *   1-6         直接进入：MUSIC VOCAB TV-REMOTE DICE NOISE DIAG
    ```
    并把第 2 行「两个 app」改成「几个 app」
- **README**：顶部菜单图换成：
  ```
  STACKM5              87% 4.01V
          ┌─────────┐
    ♪♪    │  (书)   │   (电视)
   MUSIC  │  VOCAB  │ TV REMOTE
          └─────────┘
           ● ○ ○ ○ ○ ○
  ,/ move SPC/ENT open 1-6
  ```
  下面加一句：「`,` `/` 左右转，空格或回车进入；1–6 直接进入，顺序是 MUSIC VOCAB TV REMOTE DICE NOISE DIAG」；
  代码结构清单加 `lib/carousel/` 和 `src/menu_app.cpp` 两行；用例数按实际更新
- **记忆** `cardputer-menu-diag-last.md`：「改三处」改成「改 `menu_app.cpp` 的 item 表和 README 菜单说明两处」

## 不做

- 不存 NVS、不做开机动画、不做按住连转（键盘驱动只报按下变化）
- 不改各 app 自己的页面

## 验证

- `test/test_carousel`：
  - `wrap` 的负数和越界
  - 右转到头绕回 0，左转到头绕回 n−1
  - `step` 后 offset 从 ±1 单调缓动到 0，`kStepMs` 之后为 0 且 `animating` 为 false
  - 连按 5 次：`selected` = 起点 + 5 取模，offset 不超过 1.5
  - `jump` 设好当前项且 offset 立刻为 0
  - `slotPos` 绕回到最近一侧（上面三个例子），距离恰好 n/2 时取正
  - `slotGeom` 左右对称、中间最大最亮、`|pos| ≥ 2` 不可见
  - `dim565(c, 1) == c`、`dim565(0xFFFF, 0) == 0`、`dim565(0xFFFF, 0.5)` 每个通道约为一半
- `make test` 全过，`make build` 编译通过
- 实机检查（烧写前先用 `flash_id` 确认是 8MB 的 Adv）：转动顺滑不闪、6 个图标都能认出来、
  空格和回车都能进 app、返回后还停在原来那项、数字键依然有效
