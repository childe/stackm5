#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "btdev.h"

using btdev::Entry;
using btdev::Key;
using btdev::Live;
using btdev::PairingWindow;
using btdev::Role;
using btdev::Store;

void setUp(void)
{
}

void tearDown(void)
{
}

static Key mk(uint8_t last, uint8_t type = 0)
{
    const uint8_t val[6] = {last, 0x22, 0x33, 0x44, 0x55, 0x66};
    return btdev::addrKey(val, type);
}

void test_addr_roundtrip_and_format(void)
{
    const uint8_t val[6] = {0x56, 0x34, 0x12, 0x38, 0xc1, 0xa4};
    const Key k = btdev::addrKey(val, 1);
    TEST_ASSERT_EQUAL_STRING("a4:c1:38:12:34:56", btdev::formatAddr(k).c_str());

    uint8_t back[6] = {};
    uint8_t type = 9;
    btdev::keyToVal(k, back, type);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(val, back, 6);
    TEST_ASSERT_EQUAL_UINT8(1, type);

    TEST_ASSERT_TRUE(btdev::addrKey(val, 0) != btdev::addrKey(val, 1));
}

void test_merge_role(void)
{
    TEST_ASSERT_TRUE(Role::Tv == btdev::mergeRole(true, Role::Tv, Role::Mac));
    TEST_ASSERT_TRUE(Role::Mac == btdev::mergeRole(true, Role::Unknown, Role::Mac));
    TEST_ASSERT_TRUE(Role::Mac == btdev::mergeRole(false, Role::Tv, Role::Mac));
    TEST_ASSERT_TRUE(Role::Unknown == btdev::mergeRole(false, Role::Unknown, Role::Unknown));
}

void test_store_upsert_and_remove(void)
{
    Store s;
    const Key k = mk(1);
    s.setName(k, "TV");
    s.setRole(k, Role::Tv);
    TEST_ASSERT_EQUAL_UINT32(1, s.size());
    const btdev::Record *r = s.find(k);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_STRING("TV", r->name);
    TEST_ASSERT_TRUE(Role::Tv == r->role);
    TEST_ASSERT_TRUE(s.remove(k));
    TEST_ASSERT_EQUAL_UINT32(0, s.size());
    TEST_ASSERT_FALSE(s.remove(k));
}

void test_store_long_name_truncated_utf8_safe(void)
{
    Store s;
    const Key k = mk(1);
    const std::string zh = std::string(28, 'a') + "\xe4\xb8\xad";  // 中
    s.setName(k, zh.c_str());
    TEST_ASSERT_EQUAL_STRING(std::string(28, 'a').c_str(), s.find(k)->name);

    s.setName(k, std::string(40, 'a').c_str());
    TEST_ASSERT_EQUAL_STRING(std::string(29, 'a').c_str(), s.find(k)->name);
}

void test_store_full_drops_oldest(void)
{
    Store s;
    for (uint8_t i = 1; i <= 9; ++i) s.setRole(mk(i), Role::Tv);
    TEST_ASSERT_EQUAL_UINT32(8, s.size());
    TEST_ASSERT_NULL(s.find(mk(1)));
    TEST_ASSERT_NOT_NULL(s.find(mk(9)));
}

void test_store_serialize_roundtrip(void)
{
    Store s;
    s.setName(mk(1), "Living Room TV");
    s.setRole(mk(1), Role::Tv);
    s.setRole(mk(2, 1), Role::Mac);
    const std::vector<uint8_t> bytes = s.serialize();

    Store t;
    TEST_ASSERT_TRUE(t.load(bytes));
    TEST_ASSERT_EQUAL_UINT32(2, t.size());
    for (size_t i = 0; i < 2; ++i) {
        TEST_ASSERT_TRUE(s.at(i).key == t.at(i).key);
        TEST_ASSERT_TRUE(s.at(i).role == t.at(i).role);
        TEST_ASSERT_EQUAL_STRING(s.at(i).name, t.at(i).name);
    }
}

void test_store_load_rejects_bad_bytes(void)
{
    Store s;
    s.setName(mk(1), "A");
    std::vector<uint8_t> good = s.serialize();

    Store t;
    t.setRole(mk(5), Role::Mac);
    TEST_ASSERT_FALSE(t.load(std::vector<uint8_t>()));
    TEST_ASSERT_EQUAL_UINT32(0, t.size());

    std::vector<uint8_t> badVer = good;
    badVer[0] = 2;
    t.setRole(mk(5), Role::Mac);
    TEST_ASSERT_FALSE(t.load(badVer));
    TEST_ASSERT_EQUAL_UINT32(0, t.size());

    std::vector<uint8_t> shortBytes(good.begin(), good.end() - 1);
    t.setRole(mk(5), Role::Mac);
    TEST_ASSERT_FALSE(t.load(shortBytes));
    TEST_ASSERT_EQUAL_UINT32(0, t.size());
}

void test_merge_union_and_labels(void)
{
    const Key A = mk(1), B = mk(2), C = mk(3);
    Store s;
    s.setName(A, "Living Room TV");
    s.setRole(A, Role::Tv);
    std::vector<Live> live = {{C, Role::Mac, "MacBook Pro"}};
    std::vector<Entry> out = btdev::merge(s, {A, B}, live);
    TEST_ASSERT_EQUAL_UINT32(3, out.size());

    TEST_ASSERT_TRUE(out[0].key == A);
    TEST_ASSERT_EQUAL_STRING("Living Room TV", out[0].label.c_str());
    TEST_ASSERT_TRUE(out[0].bonded);
    TEST_ASSERT_FALSE(out[0].connected);
    TEST_ASSERT_TRUE(Role::Tv == out[0].role);

    TEST_ASSERT_TRUE(out[1].key == C);
    TEST_ASSERT_EQUAL_STRING("MacBook Pro", out[1].label.c_str());
    TEST_ASSERT_TRUE(out[1].connected);
    TEST_ASSERT_FALSE(out[1].bonded);
    TEST_ASSERT_TRUE(Role::Mac == out[1].role);

    TEST_ASSERT_TRUE(out[2].key == B);
    TEST_ASSERT_EQUAL_STRING(btdev::formatAddr(B).c_str(), out[2].label.c_str());
    TEST_ASSERT_TRUE(Role::Unknown == out[2].role);
    TEST_ASSERT_TRUE(out[2].bonded);
}

void test_merge_name_priority(void)
{
    const Key A = mk(1);
    Store s;
    s.setRole(A, Role::Tv);
    std::vector<Live> live = {{A, Role::Unknown, "X"}};
    std::vector<Entry> out = btdev::merge(s, {A}, live);
    TEST_ASSERT_EQUAL_UINT32(1, out.size());
    TEST_ASSERT_EQUAL_STRING("X", out[0].label.c_str());

    s.setName(A, "S");
    out = btdev::merge(s, {A}, live);
    TEST_ASSERT_EQUAL_STRING("S", out[0].label.c_str());
}

void test_merge_sort(void)
{
    const Key k1 = mk(1), k2 = mk(2), k3 = mk(3), k4 = mk(4), k5 = mk(5);
    Store s;
    s.setName(k1, "zeta");  // Unknown, 未连
    s.setName(k2, "beta");
    s.setRole(k2, Role::Mac);  // Mac, 未连
    s.setName(k3, "alpha");
    s.setRole(k3, Role::Tv);  // Tv, 未连
    s.setName(k4, "omega");
    s.setRole(k4, Role::Tv);  // Tv, 连着
    s.setName(k5, "gamma");
    s.setRole(k5, Role::Tv);  // Tv, 未连
    std::vector<Live> live = {{k4, Role::Unknown, ""}};
    std::vector<Entry> out = btdev::merge(s, {}, live);
    TEST_ASSERT_EQUAL_UINT32(5, out.size());
    TEST_ASSERT_TRUE(out[0].key == k4);
    TEST_ASSERT_TRUE(out[1].key == k3);
    TEST_ASSERT_TRUE(out[2].key == k5);
    TEST_ASSERT_TRUE(out[3].key == k2);
    TEST_ASSERT_TRUE(out[4].key == k1);
}

void test_merge_connected_role_overrides(void)
{
    const Key A = mk(1);
    Store s;
    s.setRole(A, Role::Mac);
    std::vector<Entry> out = btdev::merge(s, {A}, {{A, Role::Tv, ""}});
    TEST_ASSERT_TRUE(Role::Tv == out[0].role);
    TEST_ASSERT_TRUE(out[0].connected);
    out = btdev::merge(s, {A}, {{A, Role::Unknown, ""}});
    TEST_ASSERT_TRUE(Role::Mac == out[0].role);
}

void test_relocate_cursor(void)
{
    const Key V = 100, A = 1, B = 2, P = 200;
    TEST_ASSERT_EQUAL_INT(1, btdev::relocateCursor({V, A, B, P}, 2, {V, B, P}));
    TEST_ASSERT_EQUAL_INT(2, btdev::relocateCursor({V, A, B, P}, 2, {V, A, P}));
    TEST_ASSERT_EQUAL_INT(1, btdev::relocateCursor({V, A, B, P}, 3, {V, P}));
    TEST_ASSERT_EQUAL_INT(1, btdev::relocateCursor({V, A, B}, 2, {V, A}));
    TEST_ASSERT_EQUAL_INT(0, btdev::relocateCursor({V, A, B, P}, 2, {}));
}

void test_pairing_window(void)
{
    const Key A = mk(1), B = mk(2);
    PairingWindow w;
    w.start(1000, {A});
    TEST_ASSERT_FALSE(w.onBonded(A));
    TEST_ASSERT_TRUE(w.active());
    TEST_ASSERT_EQUAL_INT(120, w.remainingSec(1000));
    TEST_ASSERT_EQUAL_INT(120, w.remainingSec(1001));
    TEST_ASSERT_EQUAL_INT(1, w.remainingSec(120999));
    TEST_ASSERT_FALSE(w.expired(120999));
    TEST_ASSERT_TRUE(w.expired(121000));
    TEST_ASSERT_FALSE(w.active());
    TEST_ASSERT_EQUAL_INT(0, w.remainingSec(121000));

    w.start(0, {A});
    TEST_ASSERT_TRUE(w.onBonded(B));
    TEST_ASSERT_FALSE(w.active());
    TEST_ASSERT_FALSE(w.onBonded(B));
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_addr_roundtrip_and_format);
    RUN_TEST(test_merge_role);
    RUN_TEST(test_store_upsert_and_remove);
    RUN_TEST(test_store_long_name_truncated_utf8_safe);
    RUN_TEST(test_store_full_drops_oldest);
    RUN_TEST(test_store_serialize_roundtrip);
    RUN_TEST(test_store_load_rejects_bad_bytes);
    RUN_TEST(test_merge_union_and_labels);
    RUN_TEST(test_merge_name_priority);
    RUN_TEST(test_merge_sort);
    RUN_TEST(test_merge_connected_role_overrides);
    RUN_TEST(test_relocate_cursor);
    RUN_TEST(test_pairing_window);
    return UNITY_END();
}
