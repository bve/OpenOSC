#pragma once
#include "bridge.hpp"
#include <atomic>
#include "host/ble_gap.h"
#include "host/ble_gatt.h"

namespace openosc::companion {
class Bluetooth {
public:
    explicit Bluetooth(Bridge &bridge) : bridge_(bridge) {}
    bool enable(bool on, const char *name, uint32_t passkey);
    bool enabled() const { return enabled_.load(); }
    void disconnect();
    void shutdown();
    void poll(uint32_t now);
private:
    static void synced();
    static void host(void *);
    static int event(ble_gap_event *, void *);
    static int access(uint16_t, uint16_t, ble_gatt_access_ctxt *, void *);
    void advertise();
    void connectBridge(uint16_t handle);
    inline static Bluetooth *current_ = nullptr;
    Bridge &bridge_;
    std::atomic<bool> enabled_{false}, synced_{false}, subscribed_{false};
    std::atomic<uint16_t> handle_{BLE_HS_CONN_HANDLE_NONE};
    uint16_t notify_ = 0;
    uint8_t address_ = 0;
    uint32_t passkey_ = 0;
    bool initialized_ = false;
};
}
