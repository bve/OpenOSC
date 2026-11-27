#include "bluetooth.hpp"
#include <algorithm>
#include <cstring>
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/ble_sm.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

extern "C" void ble_store_config_init(void);
namespace openosc::companion {
namespace {
// Nordic UART Service UUIDs, little-endian as required by NimBLE.
const ble_uuid128_t service = BLE_UUID128_INIT(0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x01,0x00,0x40,0x6e);
const ble_uuid128_t rx = BLE_UUID128_INIT(0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x02,0x00,0x40,0x6e);
const ble_uuid128_t tx = BLE_UUID128_INIT(0x9e,0xca,0xdc,0x24,0x0e,0xe5,0xa9,0xe0,0x93,0xf3,0xa3,0xb5,0x03,0x00,0x40,0x6e);
uint32_t nowMs() { return esp_timer_get_time() / 1000; }
}

bool Bluetooth::enable(bool on, const char *name, uint32_t pin) {
    if (on == enabled_) return true;
    if (!on) {
        enabled_ = false;
        ble_gap_adv_stop();
        disconnect();
        return true;
    }
    if (!initialized_) {
        if (nimble_port_init() != ESP_OK) return false;
        current_ = this;
        passkey_ = pin;
        ble_svc_gap_init(); ble_svc_gatt_init();
        ble_svc_gap_device_name_set(name);
        static ble_gatt_chr_def characteristics[3]{};
        characteristics[0].uuid = &rx.u;
        characteristics[0].access_cb = access;
        characteristics[0].arg = this;
        characteristics[0].flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN;
        characteristics[1].uuid = &tx.u;
        characteristics[1].access_cb = access;
        characteristics[1].arg = this;
        characteristics[1].val_handle = &notify_;
        characteristics[1].flags = BLE_GATT_CHR_F_NOTIFY;
        static ble_gatt_svc_def services[2]{};
        services[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
        services[0].uuid = &service.u;
        services[0].characteristics = characteristics;
        if (ble_gatts_count_cfg(services) || ble_gatts_add_svcs(services)) {
            nimble_port_deinit();
            current_ = nullptr;
            return false;
        }
        ble_hs_cfg.sync_cb = synced;
        ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
        ble_hs_cfg.sm_bonding = 1;
        ble_hs_cfg.sm_mitm = 1;
        ble_hs_cfg.sm_sc = 1;
        ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
        ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
        ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
        ble_store_config_init();
        initialized_ = true;
        enabled_ = true;
        nimble_port_freertos_init(host);
    } else {
        enabled_ = true;
        advertise();
    }
    return true;
}
void Bluetooth::disconnect() {
    const auto handle = handle_.load();
    if (handle != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
    bridge_.close(Bridge::Owner::Ble);
}
void Bluetooth::shutdown() {
    if (!initialized_) return;
    enable(false, nullptr, 0);
    if (!nimble_port_stop()) nimble_port_deinit();
    current_ = nullptr;
    initialized_ = false;
}
void Bluetooth::synced() {
    auto &self = *current_;
    self.synced_ = ble_hs_id_infer_auto(0, &self.address_) == 0;
    self.advertise();
}
void Bluetooth::host(void *) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}
void Bluetooth::advertise() {
    if (!synced_ || !enabled_ || handle_ != BLE_HS_CONN_HANDLE_NONE || ble_gap_adv_active()) return;
    ble_hs_adv_fields fields{};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &service; fields.num_uuids128 = 1; fields.uuids128_is_complete = 1;
    if (ble_gap_adv_set_fields(&fields)) return;
    ble_hs_adv_fields scan{};
    scan.name = reinterpret_cast<const uint8_t *>(ble_svc_gap_device_name());
    scan.name_len = std::strlen(ble_svc_gap_device_name()); scan.name_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&scan);
    ble_gap_adv_params params{};
    params.conn_mode = BLE_GAP_CONN_MODE_UND; params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(address_, nullptr, BLE_HS_FOREVER, &params, event, this);
}
void Bluetooth::connectBridge(uint16_t handle) {
    ble_gap_conn_desc info{};
    if (!enabled_ || !subscribed_ || ble_gap_conn_find(handle, &info) || !info.sec_state.encrypted || !info.sec_state.authenticated) return;
    if (!bridge_.active(Bridge::Owner::Ble) && !bridge_.open(Bridge::Owner::Ble, nowMs()))
        ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
}
int Bluetooth::event(ble_gap_event *e, void *context) {
    auto &self = *static_cast<Bluetooth *>(context);
    switch (e->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (!e->connect.status) {
            self.handle_ = e->connect.conn_handle;
            self.subscribed_ = false;
            ble_gap_security_initiate(e->connect.conn_handle);
        } else self.advertise();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        self.handle_ = BLE_HS_CONN_HANDLE_NONE;
        self.subscribed_ = false;
        self.bridge_.close(Bridge::Owner::Ble);
        self.advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE: self.advertise(); break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (e->subscribe.attr_handle == self.notify_) {
            self.subscribed_ = e->subscribe.cur_notify;
            if (self.subscribed_) self.connectBridge(e->subscribe.conn_handle);
            else self.bridge_.close(Bridge::Owner::Ble);
        }
        break;
    case BLE_GAP_EVENT_ENC_CHANGE: self.connectBridge(e->enc_change.conn_handle); break;
    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (e->passkey.params.action == BLE_SM_IOACT_DISP) {
            ble_sm_io io{};
            io.action = BLE_SM_IOACT_DISP; io.passkey = self.passkey_;
            ble_sm_inject_io(e->passkey.conn_handle, &io);
        }
        break;
    default: break;
    }
    return 0;
}
int Bluetooth::access(uint16_t handle, uint16_t, ble_gatt_access_ctxt *ctxt, void *context) {
    auto &self = *static_cast<Bluetooth *>(context);
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR || handle != self.handle_ || !self.enabled_)
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    uint8_t bytes[512];
    uint16_t count = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, bytes, sizeof bytes, &count) || self.bridge_.space(Bridge::Owner::Ble) < count)
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    return self.bridge_.input(Bridge::Owner::Ble, bytes, count, nowMs()) == count ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}
void Bluetooth::poll(uint32_t now) {
    using Owner = Bridge::Owner;
    const auto handle = handle_.load();
    if (!enabled_ || handle == BLE_HS_CONN_HANDLE_NONE || !subscribed_) return;
    if (!bridge_.active(Owner::Ble)) return;
    const auto mtu = ble_att_mtu(handle);
    if (mtu < 4) return;
    uint8_t bytes[244];
    const size_t count = bridge_.output(Owner::Ble, bytes, std::min<size_t>(sizeof bytes, mtu - 3));
    if (!count) return;
    auto *packet = ble_hs_mbuf_from_flat(bytes, count);
    if (packet && !ble_gatts_notify_custom(handle, notify_, packet) && handle == handle_)
        bridge_.sent(Owner::Ble, count, now);
}
}
