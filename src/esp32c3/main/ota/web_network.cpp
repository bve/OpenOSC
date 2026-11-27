#include "web_network.hpp"
#include "network_identity.hpp"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/task.h"

namespace openosc::companion::ota {
namespace {
constexpr char Key[] = "web_ota_v1";
}

WebNetwork loadWebNetwork() {
    nvs_handle_t nvs;
    WebNetwork network{};
    if (nvs_open(SettingsNamespace, NVS_READONLY, &nvs) != ESP_OK) return network;
    size_t size = sizeof network;
    const auto read = nvs_get_blob(nvs, Key, &network, &size);
    nvs_close(nvs);
    if (read != ESP_OK || size != sizeof network ||
        (network.mode != Mode::AccessPoint && (network.mode != Mode::Station || !network.address.ip.addr))) return {};
    return network;
}

bool saveWebNetwork(const WebNetwork &network) {
    nvs_handle_t nvs;
    if (nvs_open(SettingsNamespace, NVS_READWRITE, &nvs) != ESP_OK) return false;
    const bool saved = nvs_set_blob(nvs, Key, &network, sizeof network) == ESP_OK && nvs_commit(nvs) == ESP_OK;
    nvs_close(nvs);
    return saved;
}

bool clearWebNetwork() {
    nvs_handle_t nvs;
    if (nvs_open(SettingsNamespace, NVS_READWRITE, &nvs) != ESP_OK) return false;
    const auto result = nvs_erase_key(nvs, Key);
    const bool cleared = result == ESP_ERR_NVS_NOT_FOUND || (result == ESP_OK && nvs_commit(nvs) == ESP_OK);
    nvs_close(nvs);
    return cleared;
}

bool captureWebNetwork(WebNetwork &network) {
    wifi_mode_t current;
    if (esp_wifi_get_mode(&current) != ESP_OK) return false;
    network = {};
    if (current == WIFI_MODE_AP || current == WIFI_MODE_APSTA) {
        network.mode = Mode::AccessPoint;
        return true;
    }
    if (current != WIFI_MODE_STA) return false;
    network.mode = Mode::Station;
    const auto station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    return station && esp_netif_is_netif_up(station) && esp_netif_get_ip_info(station, &network.address) == ESP_OK &&
           network.address.ip.addr && network.address.netmask.addr;
}

[[noreturn]] void restartOnNetwork() {
    vTaskDelay(pdMS_TO_TICKS(350)); // Allow the update reply to reach the browser.
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK && (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)) {
        // The AP comes back with the same BSSID. Clients must establish a fresh association and key.
        esp_wifi_deauth_sta(0);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    esp_wifi_stop();
    esp_restart();
}
}
