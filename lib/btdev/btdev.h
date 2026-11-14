// 蓝牙设备列表的纯逻辑 —— 零硬件依赖，可以在电脑上单元测试。
//
// 三块数据：
//   - 持久设备记录 Store：按身份地址存名字和已确认角色，序列化后放 NVS
//   - 当前绑定（NimBLE 的绑定表）和当前连接 Live：由 src/ble.cpp 每次现取
//   - merge() 把三者并成 CONFIG 页的一行行 Entry 并排序
// 连接代次（gen）的有效性校验在 ble 层做，这里不管。
//
// 设计见 docs/superpowers/specs/2026-11-14-config-menu-header-design.md 第 4 节
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace btdev {

// (type << 48) | mac48；mac48 第 i 字节 = NimBLE 地址 val[i]（val[0] 是最低字节）
using Key = uint64_t;
constexpr Key kNoKey = 0;  // 合法地址不会是全 0 + type 0

Key addrKey(const uint8_t val[6], uint8_t type);
void keyToVal(Key k, uint8_t val[6], uint8_t &type);
// "a4:c1:38:12:34:56"：val[5] 在前，小写
std::string formatAddr(Key k);

enum class Role : uint8_t { Unknown = 0, Tv = 1, Mac = 2 };

// 连着且本次连接观察到了角色 → 用观察到的；否则用持久记录
Role mergeRole(bool connected, Role observed, Role persisted);

constexpr size_t kNameMax = 29;  // 不含结尾 0

struct Record {
    Key key;
    Role role;
    char name[kNameMax + 1];
};

// 持久设备记录表。容量 kCap，按插入顺序；满了再新增丢弃最老的 at(0)。
class Store {
   public:
    static constexpr size_t kCap = 8;

    const Record *find(Key k) const;
    // 名字截到 kNameMax 字节，不切断 UTF-8 多字节字符；无该 key 则新增
    void setName(Key k, const char *name);
    // 无该 key 则新增（名字空）
    void setRole(Key k, Role r);
    bool remove(Key k);
    size_t size() const { return _n; }
    const Record &at(size_t i) const { return _recs[i]; }

    // 版本号 1 字节、条数 1 字节，再接定长记录（key 8 字节小端 + role 1 + name kNameMax+1）
    std::vector<uint8_t> serialize() const;
    // 版本、条数或长度不符 → 清空并返回 false
    bool load(const std::vector<uint8_t> &bytes);

   private:
    Record *upsert(Key k);
    Record _recs[kCap] = {};
    size_t _n = 0;
};

// 一条当前连接
struct Live {
    Key key;
    Role observed;
    std::string name;
};

// CONFIG 页的一行
struct Entry {
    Key key;
    std::string label;  // 名字（持久记录优先，再用本次连接读到的），都没有则地址
    Role role;
    bool connected;
    bool bonded;
};

// 并集 store ∪ bonded ∪ live；排序：角色 Tv < Mac < Unknown，同角色连着的在前，再按 label 字典序
std::vector<Entry> merge(const Store &store, const std::vector<Key> &bonded, const std::vector<Live> &live);

// 列表重建后找回光标：原项还在 → 跟着它；不在 → 同序号，越界夹到末项；新表为空 → 0
int relocateCursor(const std::vector<Key> &oldRows, int oldCursor, const std::vector<Key> &newRows);

// 「配对新设备」窗口：进入时记下已有绑定，出现不在其中的新绑定就算成功
class PairingWindow {
   public:
    static constexpr uint32_t kWindowMs = 120000;
    static constexpr size_t kCap = 8;

    void start(uint32_t nowMs, const std::vector<Key> &bondedBefore);
    void stop() { _active = false; }
    bool active() const { return _active; }
    // active 且 k 不在进入时的绑定集合里 → stop() 并返回 true
    bool onBonded(Key k);
    // active 且已满 kWindowMs → stop() 并返回 true
    bool expired(uint32_t nowMs);
    // 剩余秒数，向上取整；未 active 返回 0
    int remainingSec(uint32_t nowMs) const;

   private:
    bool _active = false;
    uint32_t _startMs = 0;
    Key _before[kCap] = {};
    size_t _nBefore = 0;
};

}  // namespace btdev
