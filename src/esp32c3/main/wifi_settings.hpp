#pragma once
#include "esp_wifi.h"
#include "openosc/companion/wifi_power.hpp"
#include "network_identity.hpp"
#include <cstring>

namespace openosc::companion {
inline constexpr char WifiPowerKey[] = "wifi_power";
inline uint8_t savedWifiPower(nvs_handle_t nvs) {
    uint8_t power;
    return nvs && nvs_get_u8(nvs, WifiPowerKey, &power) == ESP_OK && wifiPowerValid(power) ? power : DefaultWifiPower;
}

inline void accessPointSettings(wifi_config_t &wifi, const char *name, const char *password) {
    const auto length = std::strlen(name);
    std::memcpy(wifi.ap.ssid, name, length);
    std::memcpy(wifi.ap.password, password, std::strlen(password));
    wifi.ap.ssid_len = length;
    wifi.ap.channel = 1;
    wifi.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi.ap.max_connection = 3;
}

inline void stationSettings(wifi_config_t &wifi, const char *ssid, const char *password) {
    std::memcpy(wifi.sta.ssid, ssid, std::strlen(ssid));
    std::memcpy(wifi.sta.password, password, std::strlen(password));
    wifi.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi.sta.threshold.authmode = *password ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
}
}
