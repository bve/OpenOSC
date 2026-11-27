#include "network_identity.hpp"
#include "esp_mac.h"
#include <cstdio>

namespace openosc::companion {
bool networkName(char (&name)[ApNameBytes + 1]) {
    uint8_t mac[6]{};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) return false;
    std::snprintf(name, sizeof name, "OpenOSC-%02X%02X%02X", mac[3], mac[4], mac[5]);
    return true;
}
bool validApPassword(const char *password) {
    const auto size = std::strlen(password);
    if (size < 8 || size > PasswordBytes) return false;
    for (const char *ch = password; *ch; ++ch)
        if (*ch < ' ' || *ch > '~') return false;
    return true;
}
void loadApPassword(nvs_handle_t nvs, char (&password)[PasswordBytes + 1]) {
    if (nvs) {
        size_t size = sizeof password;
        const auto result = nvs_get_str(nvs, ApPasswordKey, password, &size);
        if (result == ESP_OK && validApPassword(password)) return;
        // Keep the old AP password when an installed board receives its first wireless update.
        size = sizeof password;
        if (result == ESP_ERR_NVS_NOT_FOUND && nvs_get_str(nvs, "key", password, &size) == ESP_OK &&
            validApPassword(password)) return;
    }
    std::strcpy(password, DefaultApPassword);
}
bool savedStation(nvs_handle_t nvs, char (&ssid)[SsidBytes + 1], char (&password)[PasswordBytes + 1]) {
    size_t size = sizeof ssid;
    if (nvs_get_str(nvs, "ssid", ssid, &size) != ESP_OK || !*ssid) return false;
    size = sizeof password;
    return nvs_get_str(nvs, "password", password, &size) == ESP_OK;
}
}
