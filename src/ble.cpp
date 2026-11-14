#include "ble.h"

#include <NimBLEDevice.h>
#include <Preferences.h>

#include <atomic>
#include <cstring>

#include "agents_app.h"
#include "remote_app.h"

namespace {

constexpr int kMaxConns = 4;
constexpr uint16_t kNone = BLE_HS_CONN_HANDLE_NONE;

NimBLEServer *gServer = nullptr;
bool gStarted = false;
volatile bool gRemoteActive = false;
volatile bool gAgentsActive = false;

// 当前连接记录。在 NimBLE 回调线程里改、在 loop 和 ble_name 任务里读，用自旋锁护着。
// 固定数组、只做 POD 拷贝：临界区里不能 malloc
struct Conn {
    uint16_t handle;
    uint32_t gen;  // 连接代次，onConnect 时全局自增分配；0 = 已失效（被忘记）
    btdev::Key key;
    btdev::Role observed;  // 本次连接上观察到的角色
    enum NameState : uint8_t { Pending, Reading, Done, Failed } nameState;
    bool bonded;
    char name[btdev::kNameMax + 1];
};

portMUX_TYPE gMux = portMUX_INITIALIZER_UNLOCKED;
Conn gConns[kMaxConns] = {{kNone}, {kNone}, {kNone}, {kNone}};  // begin() 之前也得是「空」
uint16_t gMac = kNone;
uint32_t gNextGen = 1;

std::atomic<uint32_t> gRevision{0};

// 持久设备记录。所有修改都在 gStoreLock（FreeRTOS 互斥量）下「检查 + 写」，
// 见 commitToStore() 和 ble::forget()
btdev::Store gStore;
SemaphoreHandle_t gStoreLock = nullptr;
constexpr const char *kStoreNs = "btdev";
constexpr const char *kStoreKey = "list";

// ble_name 任务的作业队列。回调里只入队（等待时间 0），GATT 读和 NVS 写都在任务里做
struct Job {
    enum Kind : uint8_t { ReadName, Persist } kind;
    uint16_t handle;
    uint32_t gen;
};
QueueHandle_t gJobs = nullptr;

void bump()
{
    gRevision.fetch_add(1);
}

// 只在持有 gMux 时调
Conn *slotOf(uint16_t h)
{
    if (h == kNone) return nullptr;
    for (auto &c : gConns) {
        if (c.handle == h) return &c;
    }
    return nullptr;
}

void clearSlot(Conn &c)
{
    std::memset(&c, 0, sizeof(c));
    c.handle = kNone;
}

void enqueue(Job::Kind kind, uint16_t handle, uint32_t gen)
{
    if (!gJobs) return;
    const Job job{kind, handle, gen};
    xQueueSend(gJobs, &job, 0);
}

btdev::Key keyOf(const NimBLEAddress &a)
{
    return btdev::addrKey(a.getVal(), a.getType());
}

// 只在已持有 gStoreLock 时调
void saveStore()
{
    const std::vector<uint8_t> bytes = gStore.serialize();
    Preferences prefs;
    if (!prefs.begin(kStoreNs, false)) return;
    prefs.putBytes(kStoreKey, bytes.data(), bytes.size());
    prefs.end();
}

void loadStore()
{
    Preferences prefs;
    if (!prefs.begin(kStoreNs, true)) return;  // 命名空间还不存在：空表
    const size_t len = prefs.getBytesLength(kStoreKey);
    std::vector<uint8_t> bytes(len);
    if (len) prefs.getBytes(kStoreKey, bytes.data(), len);
    prefs.end();
    if (len && !gStore.load(bytes)) Serial.println("[ble] device store corrupt, dropped");
}

// 把一条连接上确认到的名字 / 角色写进持久记录（只在 ble_name 任务里调）。
// 持 gStoreLock 再在 gMux 里核对 gen 和是否绑定，核对和写入之间不放锁：
// 和 forget() 只有「先写后删」或「先删、这里核对失败丢弃」两种顺序，记录不会复活
void commitToStore(uint16_t handle, uint32_t gen, const char *name, btdev::Role role)
{
    xSemaphoreTake(gStoreLock, portMAX_DELAY);

    bool ok = false;
    btdev::Key key = btdev::kNoKey;
    portENTER_CRITICAL(&gMux);
    const Conn *c = slotOf(handle);
    if (c && gen != 0 && c->gen == gen && c->bonded) {
        ok = true;
        key = c->key;
    }
    portEXIT_CRITICAL(&gMux);

    if (!ok) {
        xSemaphoreGive(gStoreLock);
        Serial.printf("[ble] commit h=%u gen stale, dropped\n", handle);
        return;
    }

    bool changed = false;
    const btdev::Record *rec = gStore.find(key);
    if (name && name[0] && (!rec || std::strncmp(rec->name, name, btdev::kNameMax) != 0)) {
        gStore.setName(key, name);
        changed = true;
    }
    rec = gStore.find(key);
    if (role != btdev::Role::Unknown && (!rec || rec->role != role)) {
        gStore.setRole(key, role);
        changed = true;
    }
    if (changed) saveStore();
    xSemaphoreGive(gStoreLock);
    if (changed) bump();
}

void doPersist(const Job &job)
{
    char name[btdev::kNameMax + 1] = {0};
    btdev::Role role = btdev::Role::Unknown;
    bool found = false;
    portENTER_CRITICAL(&gMux);
    const Conn *c = slotOf(job.handle);
    if (c && c->gen == job.gen) {
        found = true;
        std::memcpy(name, c->name, sizeof(name));
        role = c->observed;
    }
    portEXIT_CRITICAL(&gMux);
    if (!found) return;
    if (!name[0] && role == btdev::Role::Unknown) return;
    commitToStore(job.handle, job.gen, name[0] ? name : nullptr, role);
}

void nameTask(void *)
{
    Job job;
    for (;;) {
        if (xQueueReceive(gJobs, &job, portMAX_DELAY) != pdTRUE) continue;
        switch (job.kind) {
            case Job::Persist:
                doPersist(job);
                break;
            case Job::ReadName:
                break;
        }
    }
}

void refreshAdvertising()
{
    const bool needRemote = gRemoteActive && !ble::remoteConnected();
    const bool needAgents = gAgentsActive && !ble::macConnected();

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->stop();
    if (!needRemote && !needAgents) return;

    adv->clearData();
    adv->enableScanResponse(true);
    if (needRemote) adv->addServiceUUID(remote_app::serviceUUID());
    if (needAgents) adv->addServiceUUID(agents_app::serviceUUID());
    adv->setAppearance(0x03C1);       // 961 = HID Keyboard
    adv->setName("Cardputer Remote");  // 开了 scan response，名字落在 scan response 里
    adv->start();
}

// 鉴权完成 / 拿到身份地址后：身份地址和绑定状态可能刚变，更新连接记录并尝试持久化
void updateIdentity(NimBLEConnInfo &info)
{
    const uint16_t h = info.getConnHandle();
    const btdev::Key key = keyOf(info.getIdAddress());
    const bool bonded = info.isBonded();
    uint32_t gen = 0;
    portENTER_CRITICAL(&gMux);
    Conn *c = slotOf(h);
    if (c) {
        c->key = key;
        if (c->gen != 0) c->bonded = bonded;  // 已被忘记的连接不再算绑定
        gen = c->gen;
    }
    portEXIT_CRITICAL(&gMux);
    if (!c) return;
    bump();
    if (gen != 0) enqueue(Job::Persist, h, gen);
}

class ServerCb : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *, NimBLEConnInfo &info) override
    {
        const uint16_t h = info.getConnHandle();
        const btdev::Key key = keyOf(info.getIdAddress());
        const bool bonded = info.isBonded();
        portENTER_CRITICAL(&gMux);
        for (auto &c : gConns) {
            if (c.handle != kNone) continue;
            clearSlot(c);
            c.handle = h;
            c.gen = gNextGen++;
            if (gNextGen == 0) gNextGen = 1;  // 0 保留为「已失效」
            c.key = key;
            c.observed = btdev::Role::Unknown;
            c.nameState = Conn::Pending;
            c.bonded = bonded;
            break;
        }
        portEXIT_CRITICAL(&gMux);
        bump();
        remote_app::noteConnect(info);
        refreshAdvertising();  // 连上后广播会自动停，另一方还在等的话要重新开
    }
    void onDisconnect(NimBLEServer *, NimBLEConnInfo &info, int reason) override
    {
        const uint16_t h = info.getConnHandle();
        portENTER_CRITICAL(&gMux);
        const bool wasMac = (h == gMac);
        if (wasMac) gMac = kNone;
        for (auto &c : gConns) {
            if (c.handle == h) clearSlot(c);
        }
        portEXIT_CRITICAL(&gMux);
        bump();
        if (!wasMac) remote_app::noteDisconnect(reason);
        refreshAdvertising();
    }
    void onAuthenticationComplete(NimBLEConnInfo &info) override
    {
        updateIdentity(info);
    }
    void onIdentity(NimBLEConnInfo &info) override
    {
        updateIdentity(info);
    }
};

}  // namespace

void ble::begin()
{
    if (gStarted) return;
    gStarted = true;

    for (auto &c : gConns) clearSlot(c);

    NimBLEDevice::init("Cardputer Remote");
    NimBLEDevice::setPower(9);                                  // dBm，够穿一间客厅
    NimBLEDevice::setSecurityAuth(true, false, true);           // 绑定 + 安全连接，不要 MITM
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);  // Android 侧 Just Works

    gServer = NimBLEDevice::createServer();
    gServer->setCallbacks(new ServerCb());
    // 断开后要不要广播、广播哪些服务，由 refreshAdvertising() 决定
    gServer->advertiseOnDisconnect(false);

    remote_app::registerService(gServer);  // 必须在前，见 ble.h
    agents_app::registerService(gServer);
    gServer->start();

    gStoreLock = xSemaphoreCreateMutex();
    loadStore();
    gJobs = xQueueCreate(8, sizeof(Job));
    xTaskCreate(nameTask, "ble_name", 4096, nullptr, 1, nullptr);
}

void ble::setRemoteActive()
{
    gRemoteActive = true;
    refreshAdvertising();
}

void ble::setAgentsActive(bool on)
{
    gAgentsActive = on;
    if (!on) {
        portENTER_CRITICAL(&gMux);
        const uint16_t mac = gMac;
        portEXIT_CRITICAL(&gMux);
        if (mac != kNone && gServer) gServer->disconnect(mac);
    }
    refreshAdvertising();
}

void ble::claimMac(uint16_t connHandle)
{
    uint32_t gen = 0;
    bool roleChanged = false;
    portENTER_CRITICAL(&gMux);
    const bool changed = (gMac != connHandle);
    gMac = connHandle;
    Conn *c = slotOf(connHandle);
    if (c && c->observed != btdev::Role::Mac) {
        c->observed = btdev::Role::Mac;
        roleChanged = true;
        gen = c->gen;
    }
    portEXIT_CRITICAL(&gMux);
    if (roleChanged) {
        bump();
        if (gen != 0) enqueue(Job::Persist, connHandle, gen);
    }
    if (changed) refreshAdvertising();  // 这条不再算遥控器，遥控器可能又要广播了
}

void ble::claimTv(uint16_t connHandle)
{
    uint32_t gen = 0;
    bool roleChanged = false;
    portENTER_CRITICAL(&gMux);
    Conn *c = slotOf(connHandle);
    if (c && c->observed != btdev::Role::Tv) {
        c->observed = btdev::Role::Tv;
        roleChanged = true;
        gen = c->gen;
    }
    portEXIT_CRITICAL(&gMux);
    if (!roleChanged) return;
    bump();
    if (gen != 0) enqueue(Job::Persist, connHandle, gen);
}

bool ble::macConnected()
{
    portENTER_CRITICAL(&gMux);
    const bool r = gMac != kNone;
    portEXIT_CRITICAL(&gMux);
    return r;
}

bool ble::remoteConnected()
{
    bool r = false;
    portENTER_CRITICAL(&gMux);
    for (const auto &c : gConns) {
        if (c.handle != kNone && c.handle != gMac) r = true;
    }
    portEXIT_CRITICAL(&gMux);
    return r;
}

bool ble::started()
{
    return gStarted;
}

bool ble::advertising()
{
    if (!gStarted) return false;
    return NimBLEDevice::getAdvertising()->isAdvertising();
}

bool ble::anyConnected()
{
    bool r = false;
    portENTER_CRITICAL(&gMux);
    for (const auto &c : gConns) {
        if (c.handle != kNone) r = true;
    }
    portEXIT_CRITICAL(&gMux);
    return r;
}

uint32_t ble::devicesRevision()
{
    return gRevision.load();
}

std::vector<btdev::Entry> ble::devices()
{
    if (!gStarted) return {};

    // 临界区里只做 POD 拷贝，出来再构造 vector / string
    Conn snap[kMaxConns];
    int n = 0;
    portENTER_CRITICAL(&gMux);
    for (const auto &c : gConns) {
        if (c.handle != kNone) snap[n++] = c;
    }
    portEXIT_CRITICAL(&gMux);

    std::vector<btdev::Live> live;
    live.reserve(n);
    for (int i = 0; i < n; ++i) {
        live.push_back({snap[i].key, snap[i].observed, std::string(snap[i].name)});
    }

    std::vector<btdev::Key> bonded;
    const int nb = NimBLEDevice::getNumBonds();
    for (int i = 0; i < nb; ++i) bonded.push_back(keyOf(NimBLEDevice::getBondedAddress(i)));

    xSemaphoreTake(gStoreLock, portMAX_DELAY);
    const btdev::Store store = gStore;
    xSemaphoreGive(gStoreLock);

    return btdev::merge(store, bonded, live);
}

void ble::forget(btdev::Key key)
{
    if (!gStarted) return;

    xSemaphoreTake(gStoreLock, portMAX_DELAY);
    uint16_t handle = kNone;
    portENTER_CRITICAL(&gMux);
    for (auto &c : gConns) {
        if (c.handle == kNone || c.key != key) continue;
        c.bonded = false;
        c.gen = 0;  // 排队中和执行中的旧作业全部失效
        handle = c.handle;
    }
    portEXIT_CRITICAL(&gMux);
    if (gStore.remove(key)) saveStore();
    xSemaphoreGive(gStoreLock);

    if (handle != kNone && gServer) gServer->disconnect(handle);
    uint8_t val[6];
    uint8_t type = 0;
    btdev::keyToVal(key, val, type);
    NimBLEDevice::deleteBond(NimBLEAddress(val, type));
    bump();
}

bool ble::notifyMac(const std::string &line)
{
    portENTER_CRITICAL(&gMux);
    const uint16_t mac = gMac;
    portEXIT_CRITICAL(&gMux);
    if (mac == kNone) return false;
    return agents_app::notify(line, mac);
}
