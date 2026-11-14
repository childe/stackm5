#include "agents_app.h"

#include <deque>

#include "ble.h"

namespace {

constexpr const char *kServiceUuid = "a7c4f3e0-5b1d-4e8a-9c2f-1d3b6e8a0001";
constexpr const char *kWriteUuid = "a7c4f3e0-5b1d-4e8a-9c2f-1d3b6e8a0002";
constexpr const char *kNotifyUuid = "a7c4f3e0-5b1d-4e8a-9c2f-1d3b6e8a0003";
constexpr size_t kMaxQueued = 64;

NimBLEService *gService = nullptr;
NimBLECharacteristic *gNotifyChr = nullptr;

// 写回调在 NimBLE 线程里把消息排进来，loop 里取走。用 FreeRTOS 互斥量而不是自旋锁：
// 入队要分配内存，不能在关中断的临界区里做
SemaphoreHandle_t gQueueLock = nullptr;
std::deque<std::string> gQueue;

class WriteCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &info) override
    {
        ble::claimMac(info.getConnHandle());
        const std::string v = chr->getValue();
        xSemaphoreTake(gQueueLock, portMAX_DELAY);
        if (gQueue.size() >= kMaxQueued) gQueue.pop_front();
        gQueue.push_back(v);
        xSemaphoreGive(gQueueLock);
    }
};

class NotifyCb : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &info, uint16_t subValue) override
    {
        if (subValue != 0) ble::claimMac(info.getConnHandle());
    }
};

}  // namespace

void agents_app::registerService(NimBLEServer *server)
{
    gQueueLock = xSemaphoreCreateMutex();
    gService = server->createService(kServiceUuid);
    NimBLECharacteristic *w = gService->createCharacteristic(kWriteUuid, NIMBLE_PROPERTY::WRITE);
    w->setCallbacks(new WriteCb());
    gNotifyChr = gService->createCharacteristic(kNotifyUuid, NIMBLE_PROPERTY::NOTIFY);
    gNotifyChr->setCallbacks(new NotifyCb());
    gService->start();
}

NimBLEUUID agents_app::serviceUUID()
{
    return NimBLEUUID(kServiceUuid);
}

bool agents_app::notify(const std::string &line, uint16_t connHandle)
{
    return gNotifyChr->notify(reinterpret_cast<const uint8_t *>(line.data()), line.size(), connHandle);
}
