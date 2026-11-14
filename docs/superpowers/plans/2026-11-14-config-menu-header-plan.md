# CONFIG 菜单与统一标题行 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 首页新增 CONFIG（音量九档断电保存、蓝牙设备列表 / 忘记 / 配对新设备），并让所有页面的第 0 行由公共 `header` 模块绘制，右端常驻蓝牙 / 音量 / 电量。

**Architecture:** 可在 Mac 上测的纯逻辑放三个新库：`lib/hdrlayout`（标题行截断、电量数字、蓝牙图标状态）、`lib/debounce`（延迟写）、`lib/btdev`（地址键、持久设备记录、设备列表合并排序、角色合并、光标找回、配对窗口）。硬件侧新增 `src/header.*`、`src/config_app.*`，扩展 `src/ble.*`（连接记录带代次、`ble_name` 工作任务读对端名并写 NVS、忘记设备、配对模式）和 `src/volume.*`（NVS）。所有页面改调 `header::draw`。

**Tech Stack:** C++17，Arduino-ESP32，M5Cardputer / M5GFX（LovyanGFX），NimBLE-Arduino 2.5.x（含其内置 NimBLE host C API），Preferences（NVS），FreeRTOS 队列 / 任务，PlatformIO native + Unity。

**Spec:** `docs/superpowers/specs/2026-11-14-config-menu-header-design.md`（执行者两份都读）

## Global Constraints

- 屏幕 240x135，`fonts::AsciiFont8x16`，8x16 等宽，一行 30 列。
- 标题行 y=0..15。文字区 x=0..167（21 列）；状态段 x=168..239，像素分配严格按 spec 第 1 节表格。
- 菜单 `kItems` 是唯一来源；CONFIG 插在 DIAG 前，DIAG 永远最后；数字直达 1-8（CONFIG=7，DIAG=8）。
- HID 服务必须仍是第一个注册的服务（`ble::begin()` 里 `remote_app::registerService` 在前），不新增、不调换 GATT 服务。
- NimBLE 回调（host 任务）里不做阻塞操作：不写 NVS、不做 GATT 读、队列发送等待时间为 0。
- 主循环不做任何 GATT 操作。
- `ble_name` 任务：低优先级（1），栈 4096 字节，在 `ble::begin()` 里创建；每条连接最多读名一次；读名总超时 3000ms；读取期间绝不调用 `disconnect()`。
- 配对窗口 120 秒；进入配对不删除任何已有绑定（删掉 `deleteAllBonds()` 的任何调用路径）。
- `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` ≥ 3（NimBLE-Arduino 2.5 的 `nimconfig.h` 默认 3，满足；不改 `platformio.ini`）；`kMaxConns` 保持 4。
- 音量：NVS 命名空间 `cfg`，键 `vol`，无值默认满档（8）；1500ms 无变化后写一次。
- 电量每 5000ms 读一次 `M5.Power.getBatteryLevel()` 并缓存。
- 提交作者必须是 `childe <rmself@qq.com>`（公开 GitHub 仓库）。
- 测试：`make test`（= `pio test -e native`）；设备编译：`pio run -e adv`。

## Review Focus

- **配对时已有 3 个绑定**：NimBLE 默认 `CONFIG_BT_NIMBLE_MAX_BONDS=3`，表满时新配对会挤掉最老的绑定（可能是电视）。期望：不静默丢电视。Task 8 在 `ble::startPairing()` 里检查 `getNumBonds() >= 3` 时拒绝进入配对并让 CONFIG 底行显示 `forget a device first`（测试：Task 8 的设备验收步骤）。
- **读名期间对端断开或句柄被复用**：期望结果丢弃、不写错设备。`btdev` 不负责 gen，ble 层负责；Task 7 的设备验收含「读名中拔掉电视电源」。
- **设备名含非 ASCII 或超长**（中文电视名、32 字节以上）：期望不越界、不乱码拖垮布局。Task 3 的 `Store` 测试覆盖超长名截断到 29 字节且不切断 UTF-8 多字节字符。
- **NVS 里的设备记录损坏 / 旧版本格式**：期望丢弃后当空表，不崩。Task 3 的 `Store::load` 测试覆盖错误长度与错误版本号。
- **CONFIG 页设备列表在光标所在项被忘记、或列表变空**：期望光标落在同序号或夹到末项，不越界。Task 3 的 `relocateCursor` 测试覆盖。

---

## File Structure

| 文件 | 职责 |
|---|---|
| `lib/hdrlayout/hdrlayout.{h,cpp}` | 标题行纯逻辑：title/info 截断、电量两位数字、电池填充宽度、蓝牙图标状态 |
| `lib/debounce/debounce.{h,cpp}` | 「静默 N 毫秒后触发一次」 |
| `lib/btdev/btdev.{h,cpp}` | 地址键 / 格式化、角色合并、持久记录表（含序列化）、设备列表合并排序、光标找回、配对窗口 |
| `src/header.{h,cpp}` | 画标题行（文字 + 三个图标），`tick()` 判状态变化 |
| `src/volume.{h,cpp}` | 加 NVS 读写与 `poll()` |
| `src/ble.{h,cpp}` | 加状态查询、连接记录、`ble_name` 任务、设备快照、忘记、配对模式、`poll()` |
| `src/remote_app.cpp` | HID 输入报告订阅时 `ble::claimTv` |
| `src/config_app.{h,cpp}` | CONFIG 页 |
| `src/menu_app.{h,cpp}`、`src/main.cpp`、各 `*_app.cpp` | 改调 header，菜单加项 |
| `test/test_hdrlayout`、`test/test_debounce`、`test/test_btdev` | native 测试 |

---

### Task 1: lib/hdrlayout 标题行纯逻辑

**Files:**
- Create: `lib/hdrlayout/hdrlayout.h`, `lib/hdrlayout/hdrlayout.cpp`
- Test: `test/test_hdrlayout/test_main.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace hdrlayout {
  constexpr int kTextCols = 21;
  struct Fit { std::string title; std::string info; };
  Fit fit(const std::string &title, const std::string &info, int cols = kTextCols);
  std::string batteryDigits(int pct);   // 恒 2 字符
  int batteryFill(int pct, int innerW);  // 0..innerW
  enum class BtIcon : uint8_t { Off, Dim, Bright };
  BtIcon btIcon(bool started, bool advertising, bool connected, uint32_t nowMs);
  }
  ```

- [ ] **Step 1: 写失败测试**

  `fit` 的规则照 spec 第 1 节截断规则 1-3，按字节计列。测试：
  - `test_fit_noise_alarm`：`fit("NOISE ALARM","max 62 alarm 70")` → title `"NOIS~"`，info 原样。
  - `test_fit_viz_paused`：`fit("01 Twinkle Star","II 12:34/12:34")` → title `"01 Tw~"`。
  - `test_fit_editor`：`fit("PLAYING","1=C 4/4 120")` → title `"PLAYING"`。
  - `test_fit_agents`：`fit("AGENTS","no Mac")` → title `"AGENTS"`。
  - `test_fit_empty_info_uses_all_cols`：`fit(std::string(21,'A'),"")` → title 21 个 A；`fit(std::string(22,'A'),"")` → 20 个 A + `~`。
  - `test_fit_info_too_long`：info 为 22 个 `x` → info 为 20 个 `x` + `~`，title 为空。info 恰 21 个 → info 原样、title 为空。
  - `test_fit_n_less_than_2_hides_title`：info 19 字符 → n=1，title `"AB"` 显示为空串；info 18 字符 → n=2，title `"ABC"` → `"A~"`。
  - `test_battery_digits`：`-1`→`"--"`，`0`→`" 0"`，`7`→`" 7"`，`87`→`"87"`，`100`→`"99"`，`150`→`"99"`。
  - `test_battery_fill`：`batteryFill(0,12)==0`，`(50,12)==6`，`(100,12)==12`，`(-1,12)==0`，`(150,12)==12`。
  - `test_bt_icon`：`btIcon(false, true, true, 0) == Off`（未启动，其它参数忽略）；started 且 advertising：`nowMs=0`→`Bright`，`499`→`Bright`，`500`→`Dim`，`1000`→`Bright`（周期 1000ms，亮灭各 500ms；advertising 优先于 connected）；started、不广播、connected→`Bright`；started、不广播、未连→`Dim`。

- [ ] **Step 2: 跑测试确认失败**

  Run: `pio test -e native -f test_hdrlayout`
  Expected: 编译失败，`hdrlayout.h` 不存在。

- [ ] **Step 3: 实现上述签名**

  `fit` 规则：`if (info.size() > cols)` → info 截 `cols-1` + `~`，title 空；否则 `n = cols - info.size() - (info.empty()?0:1)`；`title.size() <= n` 原样；`n >= 2` 截 `n-1` + `~`；否则空。`cols` 用于未来别的宽度，默认 21。

- [ ] **Step 4: 跑测试确认通过**

  Run: `pio test -e native -f test_hdrlayout` → Expected: PASS（全部用例）。

- [ ] **Step 5: Commit**

  ```bash
  git add lib/hdrlayout test/test_hdrlayout
  git -c user.name=childe -c user.email=rmself@qq.com commit -m "feat(hdrlayout): 标题行截断、电量数字、蓝牙图标状态的纯逻辑"
  ```

---

### Task 2: 音量断电保存（lib/debounce + src/volume）

**Files:**
- Create: `lib/debounce/debounce.h`, `lib/debounce/debounce.cpp`
- Test: `test/test_debounce/test_main.cpp`
- Modify: `src/volume.h`, `src/volume.cpp`, `src/main.cpp`（`loop()` 加 `volume::poll()`）

**Interfaces:**
- Produces:
  ```cpp
  class Debounce {  // 全局命名空间，lib/debounce/debounce.h
   public:
    explicit Debounce(uint32_t quietMs);
    void touch(uint32_t nowMs);   // 记一次变化
    bool due(uint32_t nowMs);     // 自最后一次 touch 已静默 >= quietMs 且未触发过 → true（只一次）
  };
  namespace volume { void poll(); }  // loop 每圈调；到点写 NVS
  ```
  `volume::level()` / `levelMax()` / `up()` / `down()` / `begin()` 签名不变。

- [ ] **Step 1: 写失败测试** `test/test_debounce/test_main.cpp`
  - `test_eight_presses_one_write`：`Debounce d(1500)`；在 t=0,100,…,700 各 `touch`，然后在 t=700..2199 每 10ms 调 `due` 都为 false；t=2200 为 true；之后 t=5000 为 false。断言 true 的总次数 == 1。
  - `test_no_touch_never_due`：新建后 `due(100000)` 为 false。
  - `test_touch_again_after_fire`：触发后再 `touch(3000)`，`due(4500)` 为 true。
  - `test_wraparound`：`touch(0xFFFFFF00)`，`due(0x000005DC)` 为 true（用无符号差值）。

- [ ] **Step 2: 跑测试确认失败** — `pio test -e native -f test_debounce`，编译失败。

- [ ] **Step 3: 实现 `Debounce`**（`nowMs - last >= quietMs` 用 `uint32_t` 减法）。

- [ ] **Step 4: 跑测试确认通过** — `pio test -e native -f test_debounce` → PASS。

- [ ] **Step 5: 改 `src/volume.cpp`**

  `begin()` 用 `Preferences` 只读打开 `cfg`，读 `vol`（`getUChar("vol", 8)`），超出 0..8 用 8，再 `apply()`。`up()`/`down()` 改档成功后 `gSave.touch(millis())`（`Debounce gSave(1500)`）。`poll()`：`if (gSave.due(millis()))` 读写打开 `cfg`、`putUChar("vol", gLevel)`。头文件顶部注释里「只存在 RAM 里」改为「断电保存，1.5 秒无变化写一次 NVS」。`main.cpp` 的 `loop()` 在 `handleKeys()` 后调 `volume::poll()`。

- [ ] **Step 6: 设备编译** — `pio run -e adv` → Expected: `SUCCESS`。

- [ ] **Step 7: Commit**

  ```bash
  git add lib/debounce test/test_debounce src/volume.h src/volume.cpp src/main.cpp
  git -c user.name=childe -c user.email=rmself@qq.com commit -m "feat(volume): 档位存 NVS，连按只写一次"
  ```

---

### Task 3: lib/btdev 蓝牙设备纯逻辑

**Files:**
- Create: `lib/btdev/btdev.h`, `lib/btdev/btdev.cpp`
- Test: `test/test_btdev/test_main.cpp`

**Interfaces:**
- Produces（全部在 `namespace btdev`）：
  ```cpp
  using Key = uint64_t;                       // (type << 48) | mac48；mac48 第 i 字节 = NimBLE val[i]
  constexpr Key kNoKey = 0;                   // 合法地址不会是全 0 + type 0
  Key addrKey(const uint8_t val[6], uint8_t type);
  void keyToVal(Key k, uint8_t val[6], uint8_t &type);
  std::string formatAddr(Key k);              // "a4:c1:38:12:34:56"（val[5] 在前，小写）

  enum class Role : uint8_t { Unknown = 0, Tv = 1, Mac = 2 };
  Role mergeRole(bool connected, Role observed, Role persisted);  // 连着且 observed≠Unknown → observed；否则 persisted

  constexpr size_t kNameMax = 29;             // 不含结尾 0
  struct Record { Key key; Role role; char name[kNameMax + 1]; };
  class Store {                               // 容量 8，插入序
   public:
    static constexpr size_t kCap = 8;
    const Record *find(Key k) const;
    void setName(Key k, const char *name);    // 截到 kNameMax 字节，不切断 UTF-8；无则新增
    void setRole(Key k, Role r);              // 无则新增（名字空）
    bool remove(Key k);
    size_t size() const;
    const Record &at(size_t i) const;
    std::vector<uint8_t> serialize() const;   // 首字节版本号 1，再 1 字节条数，再定长记录
    bool load(const std::vector<uint8_t> &bytes);  // 版本或长度不符 → 清空并返回 false
  };                                          // 满了再新增：丢弃 at(0)

  struct Live { Key key; Role observed; std::string name; };  // 当前连接
  struct Entry { Key key; std::string label; Role role; bool connected; bool bonded; };
  std::vector<Entry> merge(const Store &store, const std::vector<Key> &bonded,
                           const std::vector<Live> &live);
  int relocateCursor(const std::vector<Key> &oldRows, int oldCursor, const std::vector<Key> &newRows);

  class PairingWindow {
   public:
    static constexpr uint32_t kWindowMs = 120000;
    static constexpr size_t kCap = 8;
    void start(uint32_t nowMs, const std::vector<Key> &bondedBefore);
    void stop();
    bool active() const;
    bool onBonded(Key k);                     // active 且 k 不在 bondedBefore → stop() 并返回 true
    bool expired(uint32_t nowMs);             // active 且已满 120s → stop() 并返回 true
    int remainingSec(uint32_t nowMs) const;   // 向上取整；未 active 返回 0
  };
  }
  ```

- [ ] **Step 1: 写失败测试**
  - `test_addr_roundtrip_and_format`：`val={0x56,0x34,0x12,0x38,0xc1,0xa4}`、type 1 → `formatAddr(addrKey(val,1)) == "a4:c1:38:12:34:56"`；`keyToVal` 还原同一 val 和 type；type 0 与 type 1 的 key 不相等。
  - `test_merge_role`：`mergeRole(true, Tv, Mac)==Tv`；`mergeRole(true, Unknown, Mac)==Mac`；`mergeRole(false, Tv, Mac)==Mac`；`mergeRole(false, Unknown, Unknown)==Unknown`。
  - `test_store_upsert_and_remove`：`setName(k,"TV")` 再 `setRole(k,Tv)` → size 1，name `"TV"`，role Tv；`remove(k)` → size 0，返回 true；再 remove 返回 false。
  - `test_store_long_name_truncated_utf8_safe`：名字为 28 个 `a` + `"中"`（3 字节，共 31）→ 存下的是 28 个 `a`（不留半个汉字）；40 个 `a` → 29 个 `a`。
  - `test_store_full_drops_oldest`：依次新增 9 个不同 key → size 8，第 1 个 key 找不到，第 9 个在。
  - `test_store_serialize_roundtrip`：存 2 条后 `serialize` → 新 `Store::load` 返回 true，内容相同。
  - `test_store_load_rejects_bad_bytes`：空 vector、版本号 2、长度少 1 字节 → `load` 返回 false 且 size 0。
  - `test_merge_union_and_labels`：store 有 A(name "Living Room TV", Tv)；bonded = {A, B}；live = {C(Mac 观察, name "MacBook Pro")}，C 未绑定也不在 store。结果 3 项：A label `"Living Room TV"`、bonded、未连；B label 为 `formatAddr(B)`、role Unknown；C label `"MacBook Pro"`、connected、role Mac、bonded false。
  - `test_merge_name_priority`：A 在 store 无名、live 里有名 `"X"` → label `"X"`；store 有名 `"S"`、live 名 `"X"` → label `"S"`。
  - `test_merge_sort`：排序为 role Tv < Mac < Unknown；同 role 内 connected 在前；再按 label 字典序。用 4 项打乱输入断言输出顺序。
  - `test_merge_connected_role_overrides`：store 里 A 是 Mac、live 观察到 Tv → 输出 Tv；live 观察 Unknown → 输出 Mac。
  - `test_relocate_cursor`：old `{V,A,B,P}` cursor 2(B) → new `{V,B,P}` 得 1；B 被删：old cursor 2 → new `{V,A,P}` 得 2（同序号）；new `{V,P}` 时 old cursor 3 → 1（夹到末项）；new 为空 → 0。
  - `test_pairing_window`：`start(1000,{A})`；`onBonded(A)` 为 false 且仍 active；`remainingSec(1000)==120`，`remainingSec(1001)==120`，`remainingSec(120999)==1`；`expired(120999)` false；`expired(121000)` true 且 `active()` false；再 `start(0,{A})`、`onBonded(B)` true 且 inactive；未 active 时 `onBonded(B)` false。

- [ ] **Step 2: 跑测试确认失败** — `pio test -e native -f test_btdev`，编译失败。

- [ ] **Step 3: 实现** `lib/btdev/btdev.cpp`。`merge` 收集 key 并集（store ∪ bonded ∪ live），按上面规则生成 label / role / connected / bonded，再 `std::stable_sort`。UTF-8 截断：从截断点往回跳过 `10xxxxxx` 续字节，再丢掉其前导字节。

- [ ] **Step 4: 跑测试确认通过** — `pio test -e native -f test_btdev` → PASS；再跑 `make test` 全部通过。

- [ ] **Step 5: Commit**

  ```bash
  git add lib/btdev test/test_btdev
  git -c user.name=childe -c user.email=rmself@qq.com commit -m "feat(btdev): 设备记录、列表合并、角色合并、配对窗口的纯逻辑"
  ```

---

### Task 4: header 模块 + 菜单 / DIAG 接入 + 主循环

**Files:**
- Create: `src/header.h`, `src/header.cpp`
- Modify: `src/ble.h`, `src/ble.cpp`（只加三个查询）, `src/menu_app.cpp`, `src/diag_app.cpp`, `src/main.cpp`

**Interfaces:**
- Consumes: Task 1 的 `hdrlayout::fit / batteryDigits / batteryFill / btIcon`；`volume::level()`。
- Produces:
  ```cpp
  namespace ble {
  bool started();        // begin() 跑过
  bool advertising();    // started() && NimBLEDevice::getAdvertising()->isAdvertising()
  bool anyConnected();   // 连接表里有任一非空句柄
  }
  namespace header {
  void draw(LovyanGFX &g, const char *title, const char *info = "", uint16_t bg = TFT_BLACK,
            uint16_t titleColor = TFT_WHITE, uint16_t infoColor = TFT_DARKGREY);
  bool tick();           // loop 每圈调；三项状态任一变化返回 true
  }
  ```

- [ ] **Step 1: 加 ble 查询**：`started()` 返回 `gStarted`；`advertising()` 未启动返回 false；`anyConnected()` 在自旋锁下扫 `gConns`。

- [ ] **Step 2: 实现 `src/header.cpp`**
  - `draw`：`fillRect(0,0,240,16,bg)`；`hdrlayout::fit` 得到两段；title 画在 x=0，info 右对齐到 x=167（`x = 168 - len*8`）；然后按 spec 像素表画：蓝牙图标 x=172..179（`Off`=`0x4208`，`Dim`=`TFT_DARKGREY`，`Bright`=`TFT_WHITE`；`Dim` 在闪烁灭相时也用暗灰而不是不画，保持位置可见），喇叭 x=182..189（`level()==0` 时画叉），档位数字 x=190，电池框 x=204..217 + 正极 x=218..219，填充宽 `batteryFill(pct,12)`，两位数字 x=224。状态段颜色用 `TFT_DARKGREY` / `TFT_WHITE`，背景用 `bg`。
  - `tick`：电量缓存 `gBatPct`，距上次读 ≥5000ms（首次立即）才调 `M5.Power.getBatteryLevel()`；`ble::started()` 为假时不调其它 ble 函数；快照 `{BtIcon, volume::level(), gBatPct}` 与上次不同返回 true。`draw` 只用缓存，不读 ADC。

- [ ] **Step 3: 菜单与 DIAG**
  - `menu_app.cpp`：删 `drawTitle` 与 `drawRightAligned`，改 `header::draw(g, "STACKM5")`；顶部布局注释改为「y 0..15 标题行（header 模块）」。
  - `diag_app.cpp`：第 0 行改 `header::draw(g, "DIAG")`，原各行从 row 1 开始不变（`line(g,"DIAG",row++…)` 换成 `header::draw` 后 `row = 1`）。DIAG 正文里的电压 / 百分比行保留。
  - `main.cpp` `loop()`：加 `if (header::tick()) gDirty = true;`；删除菜单页 2 秒电量重绘那段（`lastBatMs`），保留 `menu_app::tick()`。

- [ ] **Step 4: 设备编译** — `pio run -e adv` → `SUCCESS`；`make test` → 全部 PASS。

- [ ] **Step 5: 真机检查**（烧录前按记忆「烧录前先认设备」确认是 8MB Adv）
  - 菜单页第 0 行左 `STACKM5`，右端依次蓝牙（暗灰）、喇叭 + `8`、电池 + 两位数。
  - 按 `=`/`-` 在 DICE 改档，回菜单看到数字变化；静音档喇叭带叉。

- [ ] **Step 6: Commit**

  ```bash
  git add src/header.h src/header.cpp src/ble.h src/ble.cpp src/menu_app.cpp src/diag_app.cpp src/main.cpp
  git -c user.name=childe -c user.email=rmself@qq.com commit -m "feat(header): 公共标题行，菜单和 DIAG 先接入"
  ```

---

### Task 5: 其余页面改用 header

**Files:**
- Modify: `src/main.cpp`（`drawLibrary` / `drawEditor` / `drawSettings`）, `src/viz_app.cpp`, `src/dice_app.cpp`, `src/noise_app.cpp`, `src/remote_app.cpp`, `src/agents_app.cpp`

**Interfaces:**
- Consumes: Task 4 的 `header::draw`。

- [ ] **Step 1: 逐页替换第 0 行**（title / info 按 spec 第 1 节「各页面的 title / info」表）
  - LIBRARY：title `PLAYING`（`TFT_GREEN`）或 `LIBRARY`；info `"%u songs"`（去掉 `v%d`）。
  - 编辑页：title `PLAYING`（绿）或 `"%02u *"`/`"%02u"`；info `gHeaderLine`。编辑页状态行的 `v%d` 不动（spec 只要求改标题行与设置页底行）。
  - 设置页：title `SETUP`，info `"%02u"`；底行右侧 `"=- vol v%d"` 改为 `"=- vol"`。
  - 可视化：title `"%02u %s"`（id + 歌名），info `"II mm:ss/mm:ss"` 或 `"mm:ss/mm:ss"`（去掉 `v%d`），`titleColor = p.dim`，`infoColor = p.mid`。删除 `drawTopBar` 里自己的截断代码。
  - DICE：title `DICE`；info `"sum %d"` / `"no IMU"` / 空（去掉 `v%d`）。
  - NOISE：title `NOISE ALARM`（单空格）/ `NOISE`；info `"cal %d"` / `"max %d alarm %d"`（单空格）/ `"alarm %d"`；`bg` 传当前背景（闪屏时 `kFlashBg`）。
  - TV REMOTE：title `TV REMOTE`（`kFgBright`）/ `PAIRING...`（`kFgDim`）；info 空。
  - AGENTS：title `AGENTS`；info `"Mac %u"`（连着，数字为 session 数）/ `"no Mac"`；删掉原绿点和右对齐计算；闪屏亮相时 `bg = kFlashBg`。

- [ ] **Step 2: 检查没有残留** — Run: `grep -nE 'v%d' src/*.cpp`；Expected: 只剩 `main.cpp` 编辑页状态行一处。Run: `grep -n 'drawString(.*kTitleY\|kTopY' src/*.cpp`；Expected: 无输出（第 0 行全部经 header）。

- [ ] **Step 3: 设备编译 + 测试** — `pio run -e adv` → `SUCCESS`；`make test` → PASS。

- [ ] **Step 4: 真机检查**
  - NOISE 让报警触发，第 0 行显示 `NOIS~ max 62 alarm 70` 式样，报警线数值完整，闪屏时标题行背景跟着变。
  - 可视化页暂停，时间完整显示；帧率与改前目测一致（标题行不闪烁）。
  - 每页右端三图标位置一致。

- [ ] **Step 5: Commit**

  ```bash
  git add src/main.cpp src/viz_app.cpp src/dice_app.cpp src/noise_app.cpp src/remote_app.cpp src/agents_app.cpp
  git -c user.name=childe -c user.email=rmself@qq.com commit -m "refactor: 各页标题行统一走 header，去掉各自的 vN"
  ```

---

### Task 6: VOCAB 加标题行并重排

**Files:**
- Modify: `src/vocab_app.cpp`

**Interfaces:**
- Consumes: Task 4 的 `header::draw`。

- [ ] **Step 1: 改坐标**
  - 两页及两个错误屏第 0 行都 `header::draw(g, "VOCAB")`。
  - 正面：单词 y=18（大字 32px 高，到 49）；音标 y=52；释义 FreeMono12pt 从 y=70 起，**正面释义上限 2 行**（`kFrontBodyLines = 2`，70+48=118）。spec 预算：18+32+16+3×24=138 > 135，3 行放不下，按 spec 退到 2 行。
  - 例句页：小字号单词 y=18，左侧；`"%u words"` 右对齐在同一行 y=18；例句从 y=36 起 3 行（36+72=108）；`(no example)` 也在 y=36。
  - 错误屏：原 y=0 / y=16 两行改为 y=18 / y=34；`` `back `` 仍在 y=112。
  - 更新文件头的像素预算注释为新的数字。

- [ ] **Step 2: 设备编译** — `pio run -e adv` → `SUCCESS`。

- [ ] **Step 3: 真机检查**：翻 10 张卡，含 15 字符大字单词（`kBigWordMaxChars`），单词、音标、两行释义互不重叠，最底行完整在屏内。若真机上 2 行释义明显被截得太多，记录在提交说明里，不擅自改成 3 行。

- [ ] **Step 4: Commit**

  ```bash
  git add src/vocab_app.cpp
  git -c user.name=childe -c user.email=rmself@qq.com commit -m "feat(vocab): 加标题行，正文下移，正面释义改为 2 行"
  ```

---

### Task 7: ble 连接记录、读对端名、角色、设备快照、忘记设备

本任务分两次提交：**7A**（Step 1–6）先做连接记录、revision、角色、串行化的 Store 写入、设备快照和忘记设备，不读名；**7B**（Step 7–9）再加异步读名。先单独验证数据所有权和删除顺序，再引入 GATT 回调。

**Files:**
- Modify: `src/ble.h`, `src/ble.cpp`, `src/remote_app.cpp`

**Interfaces:**
- Consumes: Task 3 的 `btdev::Key / addrKey / Role / Store / Live / Entry / merge`。
- Produces:
  ```cpp
  namespace ble {
  void claimTv(uint16_t connHandle);           // NimBLE 回调线程调
  uint32_t devicesRevision();                  // std::atomic<uint32_t>
  std::vector<btdev::Entry> devices();         // loop 里调；已排序
  void forget(btdev::Key key);                 // loop 里调
  }
  ```

**Store 写入的唯一规则（7A 与 7B 都遵守）：** 所有对 `gStore` 的修改都经过以下两个函数之一，二者都先拿 `gStoreLock`（FreeRTOS 互斥量）、再在 `gMux` 内做有效性检查，检查和写入之间不放锁：
- `commitToStore(handle, gen, const char *name, btdev::Role role)`（只在 `ble_name` 任务里调）：持 `gStoreLock` → 进 `gMux` 找到该 handle 的槽，`gen` 相同且 `bonded` 才拷出 `key`，否则出 `gMux`、放锁、返回 → 出 `gMux` 后 `setName` / `setRole` → `saveStore()` → 放锁。
- `forget(key)`（loop 里调）：持 `gStoreLock` → 进 `gMux` 把 key 相同的槽 `bonded = false`、`gen = 0`（使所有排队中和执行中的旧作业失效），记下 handle → 出 `gMux` → `gStore.remove(key)` + `saveStore()` → 放锁 → 再 `disconnect(handle)`、`deleteBond`、`revision++`。
- 因为两条路径都在同一把 `gStoreLock` 下「检查 + 写」，只有两种顺序：commit 先完成、forget 随后删掉；或 forget 先完成、commit 检查失败丢弃。记录不会复活。`disconnect()` 是否已触发 `onDisconnect` 不影响结论。

- [ ] **Step 1: 连接记录替换 `gConns`**

  `struct Conn { uint16_t handle; uint32_t gen; btdev::Key key; btdev::Role observed; enum NameState : uint8_t {Pending, Reading, Done, Failed} nameState; bool bonded; char name[btdev::kNameMax + 1]; }`，固定数组 `kMaxConns = 4`，仍在 `gMux` 下读写（只 memcpy，不 malloc）。`gNextGen` 全局自增（在临界区内分配，从 1 开始，0 保留为「已失效」）。`remoteConnected()` / `macConnected()` / `anyConnected()` 语义不变，改为读 `handle` 字段。

  `onConnect`：填 handle、gen、`key = addrKey(info.getIdAddress().getVal(), info.getIdAddress().getType())`、`bonded = info.isBonded()`、`nameState = Pending`；`revision++`。`onDisconnect`：清该槽、`revision++`。新增 `onAuthenticationComplete(NimBLEConnInfo&)` 与 `onIdentity`：更新该槽的 `key` 与 `bonded`，`revision++`，入队 `{Persist, handle, gen}`。

- [ ] **Step 2: `ble_name` 工作任务（7A 只处理 Persist）**

  `begin()` 末尾：`gJobs = xQueueCreate(8, sizeof(Job))`，`xTaskCreate(nameTask, "ble_name", 4096, nullptr, 1, nullptr)`；从 NVS `btdev` / 键 `list` 读 blob 交给 `gStore.load`。`saveStore()` 只在已持有 `gStoreLock` 时调用，序列化后 `putBytes("list", …)`。

  `Job{ kind: ReadName | Persist, handle, gen }`。Persist：在 `gMux` 下拷出该槽的 `name`（非空才写）与 `observed`（非 Unknown 才写），然后调 `commitToStore(handle, gen, name, observed)`；未绑定由 `commitToStore` 的检查拦下（MAC 侧常用可变私有地址，写进去只会积垃圾）。

- [ ] **Step 3: 角色**

  `claimMac` 里同时把该槽 `observed = Mac`，入队 Persist、`revision++`。新增 `claimTv(h)`：该槽 `observed = Tv`（不改 `gMac`），入队 Persist、`revision++`。`remote_app.cpp`：给 `gKbd`、`gCc` 设同一个 `NimBLECharacteristicCallbacks` 子类，`onSubscribe` 里 `subValue != 0` 时调 `ble::claimTv(info.getConnHandle())`。

- [ ] **Step 4: `devices()` 与 `forget()`**
  - `devices()`：在 `gMux` 下只把非空槽**逐字段拷进栈上固定数组** `Conn snap[kMaxConns]` 并记下个数（POD 拷贝，不构造 vector / string，不分配内存）；出 `gMux` 后再由 `snap` 构造 `std::vector<btdev::Live>` 和其中的 `std::string`；`NimBLEDevice::getNumBonds()` / `getBondedAddress(i)` 转 key；持 `gStoreLock` 拷 `gStore`；返回 `btdev::merge(...)`。
  - `forget(key)`：按上面「Store 写入的唯一规则」实现；`deleteBond` 用 `keyToVal` 构造 `NimBLEAddress(val, type)`。

- [ ] **Step 5: 设备编译 + 测试 + 真机检查** — `pio run -e adv` → `SUCCESS`；`make test` → PASS。
  - 临时在串口打印 `devices()`：已绑定电视回连后出现一行，`TV` 角色在遥控订阅后出现，电视**不掉线**（遥控键仍有效），不重新配对、电视照常回连（HID handle 不变）。
  - **忘记与持久化的并发检查（确定性）**：临时编译开关 `-DBLE_RACE_DELAY_MS=3000`，在 `commitToStore` 里于 gen 检查**通过后**、`setName/setRole` **之前** `vTaskDelay(BLE_RACE_DELAY_MS)`。电视回连触发 `claimTv` → Persist，串口打印 `[ble] commit delayed` 后 3 秒内调用 `forget`（串口命令或临时按键）。预期：`forget` 等 commit 完成后才返回；重启后 `devices()` 里没有该电视的记录。再把延时挪到拿 `gStoreLock` **之前**重复一次，预期 commit 打印 `gen stale, dropped`，重启后同样没有记录。检查完删掉该开关。

- [ ] **Step 6: Commit（7A）**

  ```bash
  git add src/ble.h src/ble.cpp src/remote_app.cpp
  git -c user.name=childe -c user.email=rmself@qq.com commit -m "feat(ble): 连接代次、角色与设备记录、串行化 Store 写入、忘记设备"
  ```

- [ ] **Step 7: 读对端名（7B）**

  `onConnect` 末尾追加 `xQueueSend(gJobs, &job{ReadName, handle, gen}, 0)`，失败则该槽 `nameState = Failed`。

  ReadName 作业：在 `gMux` 下核对 gen，不符丢弃，否则置 `Reading`。调用 NimBLE host C API `ble_gattc_read_by_uuid(handle, 1, 0xFFFF, BLE_UUID16_DECLARE(0x2A00), cb, (void*)seq)`（不经 `NimBLEClient`，故不存在被断链或被别的 client 覆盖的问题），`seq` 是本任务自增的请求号；回调运行在 host 任务里，只把名字拷进静态缓冲并 `xTaskNotify` 本任务，`seq` 与当前请求号不符就忽略（防止超时后迟到的回调写错）。任务侧 `xTaskNotifyWait` 最长 3000ms。成功后在 `gMux` 下再核对 gen：一致则写该槽 `name`、`Done`，`revision++`，然后调 `commitToStore(handle, gen, name, Role::Unknown)`（是否绑定、是否已被忘记由 `commitToStore` 判断）；失败 / 超时 / gen 不符：`Serial.printf("[ble] name read failed h=%u rc=%d\n", …)`，该槽置 `Failed`（gen 仍一致时）。

- [ ] **Step 8: 设备编译 + 测试 + 真机检查** — `pio run -e adv` → `SUCCESS`；`make test` → PASS。
  - 已绑定电视开机回连后，串口 3 秒内出现名字或 `name read failed`，且电视**不掉线**（遥控键仍有效）。
  - 读名进行中拔掉电视电源：无崩溃，无错写。
  - 用 Step 5 的 `BLE_RACE_DELAY_MS` 开关，在 ReadName 成功后的 `commitToStore` 延时窗口内 `forget`：重启后无该记录。检查完删掉开关。

- [ ] **Step 9: Commit（7B）**

  ```bash
  git add src/ble.cpp
  git -c user.name=childe -c user.email=rmself@qq.com commit -m "feat(ble): ble_name 任务异步读对端名"
  ```

---

### Task 8: ble 配对模式

**Files:**
- Modify: `src/ble.h`, `src/ble.cpp`, `src/main.cpp`（`loop()` 调 `ble::poll()`）

**Interfaces:**
- Consumes: Task 3 的 `btdev::PairingWindow`；Task 7 的连接记录与 revision。
- Produces:
  ```cpp
  namespace ble {
  enum class PairStart { Ok, BondsFull };
  PairStart startPairing();      // loop 调
  void stopPairing();            // loop 调；未在配对时无操作
  bool pairing();
  int pairingRemainingSec();
  void poll();                   // loop 每圈调；未 started 直接返回；处理 120s 超时
  }
  ```

- [ ] **Step 1: 实现**
  - `gPairWin` 为 `btdev::PairingWindow`，读写都在 `gMux` 下（回调与 loop 都会碰）。
  - `startPairing()`：`getNumBonds() >= CONFIG_BT_NIMBLE_MAX_BONDS` 时返回 `BondsFull`、不进入（见 Review Focus 第 1 条）；否则收集当前绑定 key，`gPairWin.start(millis(), keys)`，`revision++`，`refreshAdvertising()`，返回 `Ok`。
  - `refreshAdvertising()`：`needRemote = pairing() || (gRemoteActive && !remoteConnected())`。
  - `onAuthenticationComplete`：`info.isBonded()` 且 `gPairWin.onBonded(key)` 为真 → `gRemoteActive = true`，`revision++`，`refreshAdvertising()`。
  - `stopPairing()`：active 时 `stop()`、`revision++`、`refreshAdvertising()`。
  - `poll()`：`gPairWin.expired(millis())` 为真时 `revision++`、`refreshAdvertising()`。
  - 全文 `grep -n deleteAllBonds src/` 应为空。
  - `main.cpp` `loop()` 在 `header::tick()` 前调 `ble::poll()`。

- [ ] **Step 2: 设备编译 + 测试** — `pio run -e adv` → `SUCCESS`；`make test` → PASS。

- [ ] **Step 3: Commit**

  ```bash
  git add src/ble.h src/ble.cpp src/main.cpp
  git -c user.name=childe -c user.email=rmself@qq.com commit -m "feat(ble): 配对新设备模式，保留已有绑定，120 秒超时"
  ```

（配对的真机验收在 Task 9，有了 CONFIG 页才能触发。）

---

### Task 9: CONFIG 页 + 菜单项 + README

**Files:**
- Create: `src/config_app.h`, `src/config_app.cpp`
- Modify: `src/menu_app.h`, `src/menu_app.cpp`, `src/main.cpp`, `README.md`

**Interfaces:**
- Consumes: `volume::up/down/level`；Task 7 的 `ble::devices / devicesRevision / forget`；Task 8 的 `startPairing / stopPairing / pairing / pairingRemainingSec`；Task 3 的 `btdev::relocateCursor / Role / Entry`；Task 4 的 `header::draw`。
- Produces:
  ```cpp
  namespace config_app {
  void begin();                 // ble::begin()，不调 setRemoteActive；重建列表；光标 0
  void end();                   // ble::stopPairing()
  bool tick();                  // revision 变化或配对剩余秒数变化 → 重建/重绘返回 true
  void draw(LovyanGFX &g);
  bool handleChar(char c);      // false = 回菜单（`）
  void handleEnter();
  }
  enum class App { None, Music, Vocab, Remote, Dice, Noise, Agents, Config, Diag };  // menu_app.h
  ```

- [ ] **Step 1: 实现 `config_app.cpp`**
  - 行模型：行 key 序列 = `{kVolumeRow, 设备 key…, kPairRow}`，`kVolumeRow = UINT64_MAX - 1`，`kPairRow = UINT64_MAX`。重建时用 `btdev::relocateCursor(旧行, 旧光标, 新行)`。
  - 布局：`header::draw(g, "CONFIG")`；行区 y=18 起每行 16px，可视 6 行，光标超出时滚动（同 agents 的 `scrollToCursor` 做法）；底行 y=119 提示。
  - 音量行：`"> volume"`，右侧 `"- %d +"`。
  - 设备行：x=16 角色 `TV` / `Mac` / 空；x=48 起 label，截到 21 列；连着在 x=230 画实心圆（`fillCircle(230,y+7,3,TFT_GREEN)`），未连不画。
  - pair 行：非配对 `"+ pair new device"`；配对中 `"pairing... %ds"`。
  - 底行提示（≤30 列）：音量行 `";. move =- vol \`back"`；设备行 `";. =- vol DEL forget \`back"`；pair 行 `";. =- vol ENT pair \`back"`；配对中 `";. ENT cancel \`back"`；忘记待确认 `"DEL again to forget"`（`TFT_RED`）；`startPairing` 返回 `BondsFull` 后 `"forget a device first"`（直到下一次按键）。
  - 按键：`;`/`.` 移光标；`=`/`-` 任意行调音量；`⏎` 在 pair 行切换开始 / 取消配对，配对中在任意行 `⏎` 都取消；`⌫`（main 里按现有退格键处理方式翻译成 `'\b'` 喂入）在设备行第一次置确认，第二次 `ble::forget(key)`，任何其他键清确认；`` ` `` 返回 false。
  - `tick()`：`ble::devicesRevision()` 与上次不同 → 重建列表、返回 true；配对中 `pairingRemainingSec()` 与上次显示不同 → 返回 true。

- [ ] **Step 2: 菜单与主循环**
  - `menu_app.h`：`App` 加 `Config`（在 `Diag` 前）；注释 `1-7` → `1-8`。
  - `menu_app.cpp`：`kItems` 在 DIAG 前插 `{"CONFIG", TFT_SKYBLUE, iconConfig, App::Config}`；`iconConfig` 用基本图元画齿轮（外圈 8 个齿用 `fillRect` 或 `fillTriangle`，中间 `drawCircle` 留孔），按 `stroke(s)` 走线宽；提示行 `"1-7"` → `"1-8"`。
  - `main.cpp`：`Page` 加 `Config`；`draw()` 分支；`launch()` 加 `case App::Config: config_app::begin(); gPage = Page::Config;`；按键分派加 `handleConfigKeys`（`st.enter` → `handleEnter()`；`st.del` → `handleChar('\b')`；`st.word` 逐字符，返回 false 时 `config_app::end()` 回菜单）；`loop()` 加 `if (gPage == Page::Config && config_app::tick()) gDirty = true;`；文件头注释 `1-7` 行改为 `1-8 直接进入：MUSIC VOCAB TV-REMOTE DICE NOISE AGENTS CONFIG DIAG`，并补 CONFIG 按键说明一段。
  - `README.md`：`1–7` 改 `1–8`，顺序加 CONFIG；app 表在 AGENTS 后加一行 `| **CONFIG** | 音量九档（断电保存）；蓝牙设备列表：看谁连着、忘记设备、配对新设备（不删已有绑定） |`；代码结构块加 `lib/hdrlayout/`、`lib/debounce/`、`lib/btdev/`、`src/header.cpp`、`src/config_app.cpp` 各一行；测试用例数按 `make test` 实际输出更新。

- [ ] **Step 3: 设备编译 + 测试** — `pio run -e adv` → `SUCCESS`；`make test` → PASS（记下用例总数填进 README）。

- [ ] **Step 4: 真机验收**（逐条通过才提交）
  1. 菜单按 `7` 进 CONFIG，按 `8` 进 DIAG；轮播指示点 8 个。
  2. 电视已连着时进 CONFIG：电视不掉线；列表里电视行显示名字（或地址）、`TV`、绿点。
  3. 音量行 `=` 连按 8 次，等 2 秒后断电重启，档位保持。
  4. pair 行 `⏎`：显示 `pairing... 120s` 并每秒递减；用手机 / 第二台设备配对成功后自动退出配对，新设备出现在列表；旧电视绑定仍在，且能回连。
  5. 配对中按 `⏎` 取消、或 `` ` `` 离开，广播回到常规规则（电视连着时蓝牙图标不再闪）。
  6. 设备行 `⌫` 一次显示 `DEL again to forget`，再按一次后该行消失，光标停在同序号。
  7. Mac 行（按现有生命周期：离开 AGENTS 时 `agents_app::end()` → `setAgentsActive(false)` 会断开 Mac 并停 AGENTS 广播，CONFIG 里看不到在线的 Mac）：
     - 进 AGENTS 让 Mac 连上，按 `` ` `` 回菜单再进 CONFIG。如果该 Mac 已绑定且 Persist 写过角色：显示 `Mac` 行、**无**绿点；如果未绑定：列表里没有 Mac 行。两种都算通过，记录实际是哪一种。
     - Mac 在线时的角色与连接状态：留在 AGENTS 页、Mac 连着，临时加回 Task 7 Step 5 的 `devices()` 串口打印（验收后删掉）确认 `devices()` 里该项角色为 `Mac`、已连接。绿点的绘制与 TV 行同一路径，由第 2 条覆盖。
  8. 绑定数达到 3 时在 pair 行按 `⏎`：底行显示 `forget a device first`，不广播，已有 3 个绑定都还在。

- [ ] **Step 5: Commit**

  ```bash
  git add src/config_app.h src/config_app.cpp src/menu_app.h src/menu_app.cpp src/main.cpp README.md
  git -c user.name=childe -c user.email=rmself@qq.com commit -m "feat: CONFIG 页（音量、蓝牙设备、配对新设备），菜单 1-8"
  ```
