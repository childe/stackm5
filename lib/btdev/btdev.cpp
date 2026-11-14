#include "btdev.h"

#include <algorithm>
#include <cstring>

namespace btdev {

namespace {

constexpr uint8_t kVersion = 1;
constexpr size_t kRecBytes = 8 + 1 + (kNameMax + 1);

int roleRank(Role r)
{
    switch (r) {
        case Role::Tv:
            return 0;
        case Role::Mac:
            return 1;
        default:
            return 2;
    }
}

}  // namespace

Key addrKey(const uint8_t val[6], uint8_t type)
{
    Key k = 0;
    for (int i = 0; i < 6; ++i) k |= static_cast<Key>(val[i]) << (8 * i);
    return k | (static_cast<Key>(type) << 48);
}

void keyToVal(Key k, uint8_t val[6], uint8_t &type)
{
    for (int i = 0; i < 6; ++i) val[i] = static_cast<uint8_t>(k >> (8 * i));
    type = static_cast<uint8_t>(k >> 48);
}

std::string formatAddr(Key k)
{
    static const char kHex[] = "0123456789abcdef";
    std::string s;
    for (int i = 5; i >= 0; --i) {
        const uint8_t b = static_cast<uint8_t>(k >> (8 * i));
        s += kHex[b >> 4];
        s += kHex[b & 0x0f];
        if (i > 0) s += ':';
    }
    return s;
}

Role mergeRole(bool connected, Role observed, Role persisted)
{
    if (connected && observed != Role::Unknown) return observed;
    return persisted;
}

const Record *Store::find(Key k) const
{
    for (size_t i = 0; i < _n; ++i) {
        if (_recs[i].key == k) return &_recs[i];
    }
    return nullptr;
}

Record *Store::upsert(Key k)
{
    for (size_t i = 0; i < _n; ++i) {
        if (_recs[i].key == k) return &_recs[i];
    }
    if (_n == kCap) {
        std::memmove(&_recs[0], &_recs[1], sizeof(Record) * (kCap - 1));
        --_n;
    }
    Record &r = _recs[_n++];
    std::memset(&r, 0, sizeof(r));
    r.key = k;
    r.role = Role::Unknown;
    return &r;
}

void Store::setName(Key k, const char *name)
{
    Record *r = upsert(k);
    size_t len = name ? std::strlen(name) : 0;
    if (len > kNameMax) {
        len = kNameMax;
        // 截断点落在多字节字符中间：退回该字符的前导字节，整个字符丢掉
        while (len > 0 && (static_cast<uint8_t>(name[len]) & 0xC0) == 0x80) --len;
    }
    std::memset(r->name, 0, sizeof(r->name));
    if (len) std::memcpy(r->name, name, len);
}

void Store::setRole(Key k, Role role)
{
    upsert(k)->role = role;
}

bool Store::remove(Key k)
{
    for (size_t i = 0; i < _n; ++i) {
        if (_recs[i].key != k) continue;
        std::memmove(&_recs[i], &_recs[i + 1], sizeof(Record) * (_n - i - 1));
        --_n;
        return true;
    }
    return false;
}

std::vector<uint8_t> Store::serialize() const
{
    std::vector<uint8_t> out;
    out.reserve(2 + _n * kRecBytes);
    out.push_back(kVersion);
    out.push_back(static_cast<uint8_t>(_n));
    for (size_t i = 0; i < _n; ++i) {
        const Record &r = _recs[i];
        for (int b = 0; b < 8; ++b) out.push_back(static_cast<uint8_t>(r.key >> (8 * b)));
        out.push_back(static_cast<uint8_t>(r.role));
        out.insert(out.end(), r.name, r.name + kNameMax + 1);
    }
    return out;
}

bool Store::load(const std::vector<uint8_t> &bytes)
{
    _n = 0;
    if (bytes.size() < 2 || bytes[0] != kVersion) return false;
    const size_t n = bytes[1];
    if (n > kCap || bytes.size() != 2 + n * kRecBytes) return false;
    const uint8_t *p = bytes.data() + 2;
    for (size_t i = 0; i < n; ++i, p += kRecBytes) {
        Record &r = _recs[i];
        r.key = 0;
        for (int b = 0; b < 8; ++b) r.key |= static_cast<Key>(p[b]) << (8 * b);
        const uint8_t role = p[8];
        r.role = role <= static_cast<uint8_t>(Role::Mac) ? static_cast<Role>(role) : Role::Unknown;
        std::memcpy(r.name, p + 9, kNameMax + 1);
        r.name[kNameMax] = '\0';
    }
    _n = n;
    return true;
}

std::vector<Entry> merge(const Store &store, const std::vector<Key> &bonded, const std::vector<Live> &live)
{
    std::vector<Key> keys;
    auto add = [&keys](Key k) {
        if (std::find(keys.begin(), keys.end(), k) == keys.end()) keys.push_back(k);
    };
    for (size_t i = 0; i < store.size(); ++i) add(store.at(i).key);
    for (Key k : bonded) add(k);
    for (const Live &l : live) add(l.key);

    std::vector<Entry> out;
    out.reserve(keys.size());
    for (Key k : keys) {
        const Record *rec = store.find(k);
        const Live *lv = nullptr;
        for (const Live &l : live) {
            if (l.key == k) {
                lv = &l;
                break;
            }
        }
        Entry e;
        e.key = k;
        e.connected = lv != nullptr;
        e.bonded = std::find(bonded.begin(), bonded.end(), k) != bonded.end();
        e.role = mergeRole(e.connected, lv ? lv->observed : Role::Unknown, rec ? rec->role : Role::Unknown);
        if (rec && rec->name[0]) {
            e.label = rec->name;
        } else if (lv && !lv->name.empty()) {
            e.label = lv->name;
        } else {
            e.label = formatAddr(k);
        }
        out.push_back(std::move(e));
    }
    std::stable_sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) {
        if (roleRank(a.role) != roleRank(b.role)) return roleRank(a.role) < roleRank(b.role);
        if (a.connected != b.connected) return a.connected;
        return a.label < b.label;
    });
    return out;
}

int relocateCursor(const std::vector<Key> &oldRows, int oldCursor, const std::vector<Key> &newRows)
{
    if (newRows.empty()) return 0;
    if (oldCursor >= 0 && oldCursor < static_cast<int>(oldRows.size())) {
        const Key k = oldRows[oldCursor];
        for (size_t i = 0; i < newRows.size(); ++i) {
            if (newRows[i] == k) return static_cast<int>(i);
        }
    }
    const int last = static_cast<int>(newRows.size()) - 1;
    if (oldCursor < 0) return 0;
    return oldCursor > last ? last : oldCursor;
}

void PairingWindow::start(uint32_t nowMs, const std::vector<Key> &bondedBefore)
{
    _active = true;
    _startMs = nowMs;
    _nBefore = 0;
    for (Key k : bondedBefore) {
        if (_nBefore == kCap) break;
        _before[_nBefore++] = k;
    }
}

bool PairingWindow::onBonded(Key k)
{
    if (!_active) return false;
    for (size_t i = 0; i < _nBefore; ++i) {
        if (_before[i] == k) return false;
    }
    stop();
    return true;
}

bool PairingWindow::expired(uint32_t nowMs)
{
    if (!_active) return false;
    if (static_cast<uint32_t>(nowMs - _startMs) < kWindowMs) return false;
    stop();
    return true;
}

int PairingWindow::remainingSec(uint32_t nowMs) const
{
    if (!_active) return 0;
    const uint32_t elapsed = nowMs - _startMs;
    if (elapsed >= kWindowMs) return 0;
    const uint32_t left = kWindowMs - elapsed;
    return static_cast<int>((left + 999) / 1000);
}

}  // namespace btdev
