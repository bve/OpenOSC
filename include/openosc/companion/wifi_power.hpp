#pragma once
#include <cstdint>

namespace openosc::companion {
struct WifiPowerLevel {
    uint8_t quarters;
    const char *dbm;
};

// Radio limits supported by ESP-IDF, in quarter-dBm units.
inline constexpr WifiPowerLevel WifiPowerLevels[] = {
    {8, "2"}, {20, "5"}, {28, "7"}, {34, "8.5"}, {44, "11"}, {52, "13"},
    {56, "14"}, {60, "15"}, {66, "16.5"}, {72, "18"}, {80, "20"}
};
inline constexpr uint8_t DefaultWifiPower = 34;
inline constexpr unsigned WifiPowerCount = sizeof WifiPowerLevels / sizeof *WifiPowerLevels;
inline constexpr unsigned wifiPowerIndex(uint8_t quarters) {
    for (unsigned i = 0; i < WifiPowerCount; ++i)
        if (WifiPowerLevels[i].quarters == quarters) return i;
    return WifiPowerCount;
}
inline constexpr bool wifiPowerValid(uint8_t quarters) { return wifiPowerIndex(quarters) < WifiPowerCount; }
static_assert(wifiPowerValid(DefaultWifiPower));
}
