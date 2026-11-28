#pragma once
#include "bridge.hpp"
#include "scpi_websocket.hpp"
#include "openosc/companion/wifi_power.hpp"
#include "esp_event.h"
#include "esp_netif.h"
#include "local_name.hpp"
#include "esp_wifi.h"
#include "freertos/queue.h"
#include "nvs.h"

namespace openosc::companion {
struct PortalRequest {
    enum class Action : uint8_t { Join, Mode, Services, Time, WifiPower, ApPassword } action{};
    uint8_t mode = 0, ble = 0, tcp = 0, idle = 0, power = 0;
    uint32_t epoch = 0;
    char ssid[SsidBytes + 1]{};
    char password[PasswordBytes + 1]{};
};

class Network {
public:
    explicit Network(Bridge &bridge) : bridge_(bridge) {}
    bool initialize();
    void shutdown();
    void poll(uint32_t now);
    bool canSubmit() const { return uxQueueSpacesAvailable(commands_) != 0; }
    void submit(const PortalRequest &request) { xQueueSend(commands_, &request, 0); }
    bool receive(PortalRequest &request) { return xQueueReceive(commands_, &request, 0) == pdTRUE; }

    Mode mode() const { return mode_; }
    bool mode(Mode value, uint32_t now);
    bool scan();
    bool join(const char *ssid, const char *password, uint32_t now);
    const wifi_ap_record_t *network(unsigned index) const { return index < count_ ? networks_ + index : nullptr; }
    unsigned count() const { return count_; }
    uint8_t flags() const;
    uint32_t ipv4() const;
    static uint32_t utc();
    static bool setUtc(uint32_t epoch);
    const char *name() const { return name_; }
    const char *apPassword() const { return apPassword_; }
    uint8_t wifiPower() const { return wifiPower_.load(); }
    const char *ssid() const { return ssid_; }
    uint32_t passkey() const { return passkey_; }
    bool tcp() const { return tcp_; }
    uint8_t idle() const { return idle_; }
    bool apHasClients() const { return mode_ == Mode::AccessPoint && apClients_.load(); }
    void useDebugPort(bool enabled);
    bool setTcp(bool value);
    bool setIdle(uint8_t value);
    bool setWifiPower(uint8_t value);
    bool setApPassword(const char *value, uint32_t now);
private:
    static void event(void *context, esp_event_base_t, int32_t id, void *);
    bool start(wifi_mode_t mode);
    void wifiOff();
    void closeTcp();
    void serveTcp(uint32_t now);
    void serveDns();
    void offerDns(uint32_t ip);
    Bridge &bridge_;
    ScpiWebSocket webScpi_{bridge_};
    LocalName localName_;
    esp_netif_t *sta_ = nullptr, *ap_ = nullptr;
    nvs_handle_t nvs_ = 0;
    QueueHandle_t commands_ = nullptr;
    StaticQueue_t commandsControl_{};
    uint8_t commandsStorage_[2 * sizeof(PortalRequest)]{};
    Mode mode_ = Mode::Off;
    bool up_ = false, sntp_ = false, tcp_ = false, scanning_ = false, dnsOffered_ = false, debug_ = false;
    std::atomic<bool> scanDone_{false}, apClients_{false};
    std::atomic<uint8_t> wifiPower_{DefaultWifiPower};
    uint8_t idle_ = DefaultIdleMinutes;
    unsigned count_ = 0;
    uint32_t joiningUntil_ = 0, passkey_ = 0;
    char name_[ApNameBytes + 1]{}, ssid_[SsidBytes + 1]{}, password_[PasswordBytes + 1]{};
    char apPassword_[PasswordBytes + 1]{};
    wifi_ap_record_t networks_[Networks]{};
    int listener_ = -1, client_ = -1, dns_ = -1;
};
}
