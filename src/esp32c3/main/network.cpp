#include "network.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/time.h>
#include "bootloader_random.h"
#include "network_identity.hpp"
#include "wifi_settings.hpp"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "lwip/sockets.h"
#include "dhcpserver/dhcpserver.h"

namespace openosc::companion {
namespace {
constexpr uint32_t ValidEpoch = 1704067200; // 2024-01-01 UTC.
constexpr uint16_t ScpiPort = 5025, SwdPort = 4441;
void release(int &fd) { if (fd >= 0) { close(fd); fd = -1; } }
int bound(int type, uint16_t port) {
    int fd = socket(AF_INET, type, 0);
    if (fd < 0) return -1;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    if (fcntl(fd, F_SETFL, O_NONBLOCK) != 0 || bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0)
        release(fd);
    return fd;
}
bool retry() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
}

bool Network::initialize() {
    commands_ = xQueueCreateStatic(2, sizeof(PortalRequest), commandsStorage_, &commandsControl_);
    if (!commands_ || nvs_open(SettingsNamespace, NVS_READWRITE, &nvs_) != ESP_OK ||
        !networkName(name_)) return false;
    bootloader_random_enable();
    passkey_ = esp_random() % 1000000;
    bootloader_random_disable();
    if (!savedStation(nvs_, ssid_, password_)) *ssid_ = *password_ = '\0';
    loadApPassword(nvs_, apPassword_);
    wifiPower_ = savedWifiPower(nvs_);
    uint8_t user = 0;
    if (nvs_get_u8(nvs_, "idle", &user) == ESP_OK && idleValid(user)) idle_ = user;
    if (nvs_get_u8(nvs_, "tcp", &user) == ESP_OK) tcp_ = user == 1;
    if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK) return false;
    sta_ = esp_netif_create_default_wifi_sta();
    ap_ = esp_netif_create_default_wifi_ap();
    if (!sta_ || !ap_) return false;
    wifi_init_config_t defaults = WIFI_INIT_CONFIG_DEFAULT();
    return esp_wifi_init(&defaults) == ESP_OK &&
        esp_wifi_set_storage(WIFI_STORAGE_RAM) == ESP_OK &&
        esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, event, this) == ESP_OK;
}

bool Network::start(wifi_mode_t value) {
    if (esp_wifi_set_mode(value) != ESP_OK) return false;
    if (!up_) {
        if (esp_wifi_start() != ESP_OK) return false;
        up_ = true;
        if (esp_wifi_set_max_tx_power(wifiPower()) != ESP_OK) {
            wifiOff();
            return false;
        }
    }
    return true;
}
void Network::closeTcp() {
    if (client_ >= 0) bridge_.close(Bridge::Owner::Tcp);
    release(client_);
    release(listener_);
}
void Network::wifiOff() {
    webScpi_.stop();
    closeTcp();
    release(dns_);
    dnsOffered_ = false;
    if (sntp_) { esp_netif_sntp_deinit(); sntp_ = false; }
    if (up_) esp_wifi_stop();
    up_ = scanning_ = false;
    joiningUntil_ = 0;
    scanDone_ = false;
}
void Network::shutdown() { wifiOff(); }
bool Network::mode(Mode value, uint32_t now) {
    if (unsigned(value) > unsigned(Mode::AccessPoint) || (value == Mode::Station && !*ssid_)) return false;
    wifiOff();
    mode_ = Mode::Off;
    if (value == Mode::Off) return true;
    wifi_config_t config{};
    const bool access = value == Mode::AccessPoint;
    if (access) accessPointSettings(config, name_, apPassword_);
    else stationSettings(config, ssid_, password_);
    const auto radioMode = access ? WIFI_MODE_AP : WIFI_MODE_STA;
    if (esp_wifi_set_mode(radioMode) != ESP_OK ||
        esp_wifi_set_config(access ? WIFI_IF_AP : WIFI_IF_STA, &config) != ESP_OK ||
        !start(radioMode) ||
        (!access && esp_wifi_connect() != ESP_OK)) {
        wifiOff();
        return false;
    }
    mode_ = value;
    if (!access) joiningUntil_ = now + 20000u;
    return true;
}

bool Network::join(const char *ssid, const char *password, uint32_t now) {
    const size_t size = std::strlen(ssid), secret = std::strlen(password);
    if (!size || size > SsidBytes || secret > PasswordBytes || (secret && secret < 8)) return false;
    std::memcpy(ssid_, ssid, size + 1);
    std::memcpy(password_, password, secret + 1);
    return mode(Mode::Station, now) &&
           nvs_set_str(nvs_, "ssid", ssid_) == ESP_OK &&
           nvs_set_str(nvs_, "password", password_) == ESP_OK &&
           nvs_commit(nvs_) == ESP_OK;
}

bool Network::scan() {
    if (scanning_ || joiningUntil_) return false;
    if (!start(mode_ == Mode::AccessPoint ? WIFI_MODE_APSTA : WIFI_MODE_STA)) return false;
    wifi_scan_config_t scan{};
    scan.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scanDone_ = false; count_ = 0;
    scanning_ = esp_wifi_scan_start(&scan, false) == ESP_OK;
    if (!scanning_ && mode_ == Mode::Off) wifiOff();
    return scanning_;
}
void Network::event(void *context, esp_event_base_t, int32_t, void *) { static_cast<Network *>(context)->scanDone_ = true; }
uint32_t Network::ipv4() const {
    auto *iface = mode_ == Mode::Station ? sta_ : mode_ == Mode::AccessPoint ? ap_ : nullptr;
    esp_netif_ip_info_t ip{};
    return iface && esp_netif_is_netif_up(iface) && esp_netif_get_ip_info(iface, &ip) == ESP_OK ? ntohl(ip.ip.addr) : 0;
}
uint32_t Network::utc() {
    const time_t value = std::time(nullptr);
    return value >= time_t(ValidEpoch) && uint64_t(value) <= UINT32_MAX ? uint32_t(value) : 0;
}
bool Network::setUtc(uint32_t epoch) {
    if (epoch < ValidEpoch) return false;
    timeval time{time_t(epoch), 0};
    return settimeofday(&time, nullptr) == 0;
}
uint8_t Network::flags() const {
    return (ipv4() ? Connected : 0) | (tcp_ ? Tcp : 0) | (scanning_ ? Scanning : 0) |
           (utc() ? TimeValid : 0) | (joiningUntil_ ? Joining : 0);
}
void Network::useDebugPort(bool enabled) {
    webScpi_.stop();
    closeTcp();
    debug_ = enabled;
}
bool Network::setTcp(bool value) {
    if (value == tcp_) return true;
    if (nvs_set_u8(nvs_, "tcp", value) != ESP_OK || nvs_commit(nvs_) != ESP_OK) return false;
    tcp_ = value;
    if (!tcp_) {
        webScpi_.stop();
        closeTcp();
    }
    return true;
}
bool Network::setIdle(uint8_t value) {
    if (!idleValid(value)) return false;
    if (value == idle_) return true;
    if (nvs_set_u8(nvs_, "idle", value) != ESP_OK || nvs_commit(nvs_) != ESP_OK) return false;
    idle_ = value;
    return true;
}
bool Network::setWifiPower(uint8_t value) {
    if (!wifiPowerValid(value)) return false;
    const uint8_t previous = wifiPower();
    if (value == previous) return true;
    if (up_ && esp_wifi_set_max_tx_power(value) != ESP_OK) return false;
    if (nvs_set_u8(nvs_, WifiPowerKey, value) != ESP_OK || nvs_commit(nvs_) != ESP_OK) {
        if (up_) esp_wifi_set_max_tx_power(previous);
        return false;
    }
    wifiPower_ = value;
    return true;
}
bool Network::setApPassword(const char *value, uint32_t now) {
    if (!validApPassword(value)) return false;
    if (!std::strcmp(value, apPassword_)) return true;
    if (nvs_set_str(nvs_, ApPasswordKey, value) != ESP_OK || nvs_commit(nvs_) != ESP_OK) return false;
    std::strcpy(apPassword_, value);
    return mode_ != Mode::AccessPoint || mode(Mode::AccessPoint, now);
}
void Network::poll(uint32_t now) {
    if (scanning_ && scanDone_.exchange(false)) {
        uint16_t count = Networks;
        count_ = esp_wifi_scan_get_ap_records(&count, networks_) == ESP_OK ? count : 0;
        scanning_ = false;
        if (mode_ == Mode::Off) wifiOff();
        else if (mode_ == Mode::AccessPoint) esp_wifi_set_mode(WIFI_MODE_AP);
    }
    const uint32_t ip = ipv4();
    if (joiningUntil_ && (ip || int32_t(now - joiningUntil_) >= 0)) joiningUntil_ = 0;
    if (!sntp_ && mode_ == Mode::Station && ip) {
        const esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        sntp_ = esp_netif_sntp_init(&config) == ESP_OK;
    }
    if (mode_ == Mode::AccessPoint && ip) {
        if (!dnsOffered_) offerDns(ip);
        serveDns();
    }
    if ((debug_ || tcp_) && ip) serveTcp(now);
    else closeTcp();
    if (!debug_ && tcp_ && ip) webScpi_.poll(now, ip);
    else webScpi_.stop();
}
void Network::offerDns(uint32_t ip) {
    dnsOffered_ = true;
    esp_netif_dns_info_t dns{};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = htonl(ip);
    uint8_t offer = OFFER_DNS;
    esp_netif_dhcps_stop(ap_);
    esp_netif_dhcps_option(ap_, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof offer);
    esp_netif_set_dns_info(ap_, ESP_NETIF_DNS_MAIN, &dns);
    esp_netif_dhcps_start(ap_);
}
void Network::serveTcp(uint32_t now) {
    using Owner = Bridge::Owner;
    if (listener_ < 0) {
        listener_ = bound(SOCK_STREAM, debug_ ? SwdPort : ScpiPort);
        if (listener_ >= 0 && listen(listener_, 1) != 0) release(listener_);
    }
    if (client_ < 0 && listener_ >= 0) {
        client_ = accept(listener_, nullptr, nullptr);
        if (client_ >= 0 && (fcntl(client_, F_SETFL, O_NONBLOCK) != 0 || !bridge_.open(Owner::Tcp, now))) release(client_);
    }
    if (client_ < 0) return;
    if (!bridge_.active(Owner::Tcp)) { release(client_); return; }
    const auto drop = [&] { bridge_.close(Owner::Tcp); release(client_); };
    uint8_t bytes[512];
    const auto capacity = std::min(sizeof bytes, bridge_.space(Owner::Tcp));
    if (capacity) {
        const int got = recv(client_, bytes, capacity, MSG_DONTWAIT);
        if (!got || (got < 0 && !retry())) { drop(); return; }
        if (got > 0 && bridge_.input(Owner::Tcp, bytes, got, now) != unsigned(got)) { drop(); return; }
    }
    const auto count = bridge_.output(Owner::Tcp, bytes, sizeof bytes);
    if (count) {
        const int sent = send(client_, bytes, count, MSG_DONTWAIT);
        if (sent < 0 && !retry()) drop();
        else if (sent > 0) bridge_.sent(Owner::Tcp, sent, now);
    }
}
void Network::serveDns() {
    if (dns_ < 0) dns_ = bound(SOCK_DGRAM, 53);
    if (dns_ < 0) return;
    uint8_t packet[512];
    sockaddr_in peer{};
    socklen_t peerSize = sizeof peer;
    const int size = recvfrom(dns_, packet, sizeof packet, MSG_DONTWAIT, reinterpret_cast<sockaddr *>(&peer), &peerSize);
    if (size < 17 || packet[2] & 0x80 || packet[4] || packet[5] != 1) return;
    int end = 12;
    while (end < size && packet[end] && packet[end] <= 63) end += 1 + packet[end];
    if (end + 5 > size || packet[end] || end + 21 > int(sizeof packet)) return;
    const bool address = packet[end + 1] == 0 && packet[end + 2] == 1 && packet[end + 3] == 0 && packet[end + 4] == 1;
    end += 5;
    packet[2] = 0x81; packet[3] = 0x80;
    std::memset(packet + 6, 0, 6);
    if (address) {
        packet[7] = 1;
        const uint8_t answer[] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 0, 0, 4};
        std::memcpy(packet + end, answer, sizeof answer);
        end += sizeof answer;
        const uint32_t ip = ipv4();
        for (int shift = 24; shift >= 0; shift -= 8) packet[end++] = uint8_t(ip >> shift);
    }
    sendto(dns_, packet, end, MSG_DONTWAIT, reinterpret_cast<sockaddr *>(&peer), peerSize);
}
}
