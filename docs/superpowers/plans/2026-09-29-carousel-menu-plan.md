# 首页横向图标轮播菜单 — Plan

依据 `docs/superpowers/specs/2026-09-29-carousel-menu-design.md`。分支 `feat/carousel-menu`，
提交身份 childe <rmself@qq.com>（仓库 git config 已是）。先写测试再写实现（TDD）。

## Task 1：`lib/carousel` 纯逻辑 + 单测

Files：Create `lib/carousel/carousel.h`、`lib/carousel/carousel.cpp`、`test/test_carousel/test_main.cpp`

接口（全部在 `namespace carousel`）：

```cpp
constexpr uint32_t kStepMs = 160;
constexpr float kMaxLag = 1.5f;
constexpr int kSlotSpacing = 80;   // 相邻格中心距
constexpr int kCenterX = 120;
constexpr int kBigIcon = 40;
constexpr int kSmallIcon = 28;
constexpr float kSideBright = 0.45f;

int wrap(int i, int n);                                // n <= 0 返回 0

class Carousel {
   public:
    explicit Carousel(int count);         // count < 1 按 1 处理（运行期不会出现，只是不让取模除零）
    int count() const;
    int selected() const;
    void step(int dir, uint32_t nowMs);   // dir 只看正负；dir == 0 什么都不做
    void jump(int index);                 // wrap 后设当前项，offset 归零
    float offset(uint32_t nowMs) const;   // 剩余视觉偏移（格），0 表示停稳
    bool animating(uint32_t nowMs) const;
   private:
    int _count; int _sel = 0;
    float _startOffset = 0.0f; uint32_t _startMs = 0;
};

float slotPos(int item, int sel, float offset, int n);

struct SlotGeom { int x; int size; float bright; bool visible; };
SlotGeom slotGeom(float pos);

uint16_t dim565(uint16_t c, float k);
```

语义：
- `offset(now)`：`t = (now − _startMs) / kStepMs`，`t ≥ 1` 返回 0；否则 `_startOffset · (1 − t)³`（ease-out 三次方）。
  `now < _startMs`（millis 回绕 49 天）用无符号减法，自然成立
- `animating(now)`：`offset(now) != 0.0f`
- `step(dir, now)`：`dir == 0` 直接 return；否则 `d = dir > 0 ? 1 : -1`；`_sel = wrap(_sel + d)`；
  `_startOffset = clamp(offset(now) + d, −kMaxLag, kMaxLag)`；`_startMs = now`
  （右转 `/` 时 d=+1：新当前项从右边 +1 滑到中间）
- `slotPos`：`d0 = wrap(item − sel, n)`，`d1 = d0 − n`；比较 `|d0 + offset|` 与 `|d1 + offset|`，
  取更小；相等取 d0。**返回 `d + offset`**（画面上的实际格位）
- `slotGeom(pos)`：`a = |pos|`；`x = kCenterX + round(kSlotSpacing · pos)`；
  `a ≤ 1` 时 `size = round(kBigIcon − (kBigIcon − kSmallIcon)·a)`、`bright = 1 − (1 − kSideBright)·a`；
  否则 `size = kSmallIcon`、`bright = kSideBright`；`visible = a < 2`
- `dim565`：`k ≤ 0` → 0；`k ≥ 1` → c；否则 r5/g6/b5 各自 `round(ch·k)` 后拼回

测试（先写，先看它们失败）：
1. `wrap`：(−1,6)=5、(6,6)=0、(13,6)=1、(−13,6)=5、(3,0)=0
2. 右转 6 次回到 0；左转一次从 0 到 5
3. `step(+1, 1000)` 后 `offset(1000)` = 1、`offset(1080)` 在 (0,1) 内且小于 1、单调递减、`offset(1160)` = 0、
   `animating(1160)` 为 false、`animating(1080)` 为 true；`step(−1)` 对称为负
4. 在同一时刻连按 5 次 `step(+1, t)`：`selected` = 5，`offset(t)` = 1.5（第 2 次就触顶）；
   `step(0, t)` 不改 `selected` 也不改 offset；`Carousel(0)` 的 `step` / `jump` 后 `selected` 为 0
5. `jump(4)` 后 `selected` = 4、`offset` = 0；`jump(−1)` 得 5
6. `slotPos`：(0,5,0,6) = +1；(5,0,0,6) = −1；(3,0,0,6) = +3；(1,0,0,6) = +1；(0,0,0.5,6) = 0.5
   偏移参与选圈：(5,0,+1.4,6)：d0=5→6.4，d1=−1→0.4，返回 0.4
   刚右转完：`slotPos(0, 1, 1.0, 6)` = 0（旧当前项仍在正中），`slotPos(1, 1, 1.0, 6)` = 1（新当前项在右侧）
7. `slotGeom`：`slotGeom(0)` = {120, 40, 1, true}；`slotGeom(1)`.x = 200 且 `slotGeom(−1)`.x = 40、尺寸与亮度相同（28, 0.45）；
   `slotGeom(0.5)`.size = 34；`slotGeom(2)`、`slotGeom(−2.3)` 不可见；`slotGeom(1.5)` 可见且 size = 28
8. `dim565`：(0xF81F,1) = 0xF81F；(0xFFFF,0) = 0；(0xFFFF,0.5) 的 r/g/b = 16/32/16；(0x07E0,1.2) = 0x07E0

跑 `pio test -e native -f test_carousel` 直到全绿。

## Task 2：`src/menu_app.{h,cpp}`

Files：Create `src/menu_app.h`、`src/menu_app.cpp`

```cpp
namespace menu_app {
enum class App { None, Music, Vocab, Remote, Dice, Noise, Diag };
void draw(LovyanGFX &g);
bool tick();                 // 只在菜单页调用；动画中每 16ms 返回一次 true
App handleKey(char c);       // ⏎ 由调用方翻译成 '\n'
}
```

实现要点：
- item 表 `struct Item { const char *label; uint16_t color; void (*icon)(LovyanGFX&, int cx, int cy, int s, uint16_t c); App app; }`，
  顺序 MUSIC / VOCAB / TV REMOTE / DICE / NOISE / DIAG，颜色按 design；`static carousel::Carousel gCar(kItemCount)`
- 六个图标函数，每个都只用 `s`（边长）按比例算坐标，线宽随 s（s ≥ 34 时画双线加粗）
- `menu_app.cpp` include `<M5Cardputer.h>`（电量要用 `M5.Power`）和 `<carousel.h>`；头文件只 include `<M5GFX.h>`
- `draw`：标题 + 电量（从 main.cpp 的 drawMenu 原样挪过来，含注释）→ 描边框 `drawRoundRect(82,20,76,76,6,DARKGREY)`
  （覆盖 y 20–95，最后一行像素在 95，和 design 图里的「框底 y 95」一致）→
  对每个 item 算 `slotPos` / `slotGeom`，可见就算 `c = dim565(item.color, geom.bright)`，图标和标签都用这个 c：
  在 `(x, 46)` 画尺寸 `geom.size` 的图标，在 `(x, 74)` 用 `top_center` 对齐画标签（画完恢复 `top_left`）→
  指示点（x = 95 + 10·i，y = 104，半径 2；当前项 `fillCircle(.., 2, TFT_WHITE)`，其余 `drawCircle(.., 2, TFT_DARKGREY)`）→
  提示行在 (0, kScreenH − kCharH = 119)，DARKGREY：默认 `,/ move SPC/ENT open 1-6`，当前项是 VOCAB 时 `SPC flip ENT skip ,/ move`
- `tick`：`if (!gCar.animating(millis())) return false;` 内部 `static uint32_t last`，≥16ms 才返回 true；
  动画刚结束的那一帧也要返回 true 一次，保证最后停在 offset = 0 的画面（用 `static bool wasAnimating` 实现）
- `handleKey`：`,` → `step(−1)`、`/` → `step(+1)`，返回 None；`' '`/`'\n'` → 返回 `kItems[selected].app`；
  `'1'..'6'` → `jump(c − '1')` 后返回那一项；其它 None

## Task 3：接进 `src/main.cpp`

- `#include "menu_app.h"`
- 删 `drawMenu`（电量代码已挪走），`draw()` 的 `Page::Menu` 分支改 `menu_app::draw(g)`
- 新增 `launch(menu_app::App)`（放在 `handleMenuKeys` 前），每个 case 就是原 `handleMenuKeys` 对应分支的语句：
  ```cpp
  case App::Music:  gPage = Page::Library; refreshEntries(); break;
  case App::Vocab:  vocab_app::begin();  gPage = Page::Vocab;  break;
  case App::Remote: remote_app::begin(); gPage = Page::Remote; break;
  case App::Dice:   dice_app::begin();   gPage = Page::Dice;   break;
  case App::Noise:  noise_app::begin();  gPage = Page::Noise;  break;
  case App::Diag:   diag_app::begin();   gPage = Page::Diag;   break;
  case App::None:   return;
  ```
  switch 之后 `gDirty = true`。原 Noise 分支里的 `return` 由调用方统一的「launch 后 return」取代
- `handleMenuKeys`：
  ```cpp
  if (st.enter) { const auto a = menu_app::handleKey('\n'); if (a != None) { launch(a); return; } }
  for (const char c : st.word) {        // 空格只从这里拿，不查 st.space
      const auto a = menu_app::handleKey(c);
      gDirty = true;
      if (a != None) { launch(a); return; }
  }
  ```
- `loop()` 的 `if (gPage == Page::Menu)` 块里加 `if (menu_app::tick()) gDirty = true;`，电量刷新保留
- 顶部注释的「菜单页」段按 design 替换，第 2 行改「几个 app」

## Task 4：README 与记忆

- README 顶部菜单图、说明句按 design 替换；代码结构加两行：
  `lib/carousel/      首页轮播：取模、缓动偏移、格位几何、RGB565 压暗`、`src/menu_app.cpp  首页轮播菜单（图标 + 标签）`；
  用例数改为实际数；`src/main.cpp` 行的「菜单 ↔ …」不变
- 记忆 `cardputer-menu-diag-last.md` 的 How to apply 改为「改 `src/menu_app.cpp` 的 item 表和 README 菜单说明两处」

## Task 5：验证

1. `make test` 全过（156 + 新增）
2. `make build` 成功，记录 Flash/RAM 占比
3. black 不适用（无 Python 改动）；C++ 按周围风格手工对齐
4. 实机：烧前 `esptool.py --port <p> flash_id` 确认 8MB（Adv），然后 `pio run -e adv -t upload --upload-port <p>`；
   手测 design「验证」里列的各项，外加：
   - 动画停稳后画面完全静止，中间图标正对描边框、指示点和当前项一致（验证 tick 的「结束帧再画一次」）
   - 在动画中途快速连按 `/` 五次，最后停在正确的项上、没有拖尾
   - 进 NOISE 再返回，喇叭仍然有声（`noise_app::end()` 没被破坏）
5. 提交 `feat: 首页改成横向图标轮播菜单`；推送 / PR 等用户确认

## 实机反馈修订（2026-09-29）

用户实机看过后提了两条：NOISE 图标要改；开机时 MUSIC 应该在最左，否则 DIAG 出现在第一屏很怪。

### Task R1：Carousel 支持开机当前项

- 本节覆盖 Task 1 里的构造函数签名：改为 `explicit Carousel(int count, int initial = 0);` —— `_sel = wrap(initial, _count)`，offset 为 0
- 新增测试：`Carousel(6, 1).selected() == 1`、`offset == 0`；`Carousel(6, 7).selected() == 1`；`Carousel(6, -1).selected() == 5`
- `menu_app.cpp`：`carousel::Carousel gCar(kItemCount, 1);`，在这行正上方写注释说明为什么是 1（开机第一屏 MUSIC / VOCAB / TV REMOTE，
  左边不露出绕回来的 DIAG）

### Task R2：VOCAB 提示行

- `menu_app::draw` 里 VOCAB 的提示改为 `SPC flip ENT skip ,/ move`（25 字 = 200px < 240px）

### Task R3：NOISE 图标换成分贝表盘

`iconNoise(g, cx, cy, s, c)`，全部按 s 等比例，不用 `drawArc`（避免依赖角度约定），自己按三角函数画：
- 圆心 `(cx, cy + 0.22s)`，半径 `R = 0.42s`
- 圆弧：θ 从 180° 到 360°（屏幕坐标 y 向下，点 = `(px + R cosθ, py + R sinθ)`，这个区间是上半圆），
  每 10° 一段 `drawLine`；外圈再以 `R − 1` 画一遍，s ≥ 36 时再画 `R − 2`，线宽跟 `stroke(s)` 一致
- 刻度 5 道：θ = 200°、235°、270°、305°、340°，每道从 `0.72R` 画到 `R`，1px `drawLine`（细刻度让指针成为视觉焦点）
- 指针：从圆心指向 θ = 315°（右上，偏响），长 `0.85R`，用现有 `thickLine`，粗细 `stroke(s) − 1`
- 圆心 `fillCircle(px, py, s ≥ 36 ? 3 : 2)`
- 检查范围：s = 40 时圆心 y = cy + 9、R = 17：圆弧顶 cy − 8，圆心点底 cy + 12，横向 cx ± 17，
  整体落在 cy − 8 … cy + 12，在 40×40 框（cy ± 20）内，重心约在 cy + 2，和其它图标接近

### Task R4：README / 验证

- README 菜单图改成开机第一屏的样子（左 MUSIC、中 VOCAB、右 TV REMOTE，提示行为 VOCAB 那句），说明句加「开机停在 VOCAB，MUSIC 在最左」，
  并写一句「当前项是 VOCAB 时提示行换成背单词的两个键，不显示 1-6，转到别的项就回来」
- `make test`（新增 2 个测试函数、3 条断言，用例 170 + 2 = 172）、`make build`；README 用例数改 172
- 烧前 `pio pkg exec -p tool-esptoolpy -- esptool.py --port <p> flash_id` 确认 8MB / MAC 50:78:7d:ce:6e:7c，再 `--upload-port <p>` 烧
- 实机看：开机第一屏三项对不对、NOISE 表盘能不能认出来、左右转一圈后表盘缩小变暗时是否仍清楚
