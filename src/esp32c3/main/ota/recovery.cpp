#include "boot.hpp"
#include "http_origin.hpp"
#include "network_identity.hpp"
#include "upload.hpp"
#include "watchdog.hpp"
#include "web_network.hpp"
#include "web_page.hpp"
#include "web_layout.hpp"
#include "wifi_settings.hpp"
#include "driver/gpio.h"
#include "esp_system.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <atomic>
#include <cstdio>
#include <cstring>

namespace openosc::companion {
namespace {
constexpr char Page[] = R"html(
<main>
<form class="panel mode" method="post" action="/boot">
<div><h3>Recovery mode</h3><p class="hint">The application is stopped. Start it, or install an application image.</p></div>
<button>Start installed firmware</button>
</form>
<section aria-labelledby="firmware-heading">
<h2 id="firmware-heading">Install firmware</h2>
)html";
esp_err_t recoveryPage(httpd_req_t *req) {
    if (web::begin(req) != ESP_OK || httpd_resp_sendstr_chunk(req, Page) != ESP_OK ||
        ota::updateControls(req, false) != ESP_OK || httpd_resp_sendstr_chunk(req, "</section></main>") != ESP_OK)
        return ESP_FAIL;
    return web::end(req);
}
constexpr auto WebTimeout = pdMS_TO_TICKS(120000);

bool forcedRecovery() {
    constexpr auto pin = gpio_num_t(CONFIG_BOOTLOADER_NUM_PIN_FACTORY_RESET);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
    return !gpio_get_level(pin);
}

class Recovery {
public:
    bool start(bool settingsReady) {
        if (!ota::recoveryAvailable()) return false;
        handoff_ = settingsReady ? ota::loadWebNetwork() : ota::WebNetwork{};
        const bool forced = forcedRecovery();
        if (forced) {
            ota::clearWebNetwork();
            handoff_ = {};
        }
        nvs_handle_t nvs = 0;
        if (settingsReady) nvs_open(SettingsNamespace, NVS_READWRITE, &nvs);
        char ssid[SsidBytes + 1]{}, password[PasswordBytes + 1]{};
        const bool station = handoff_.mode == Mode::Station && nvs && savedStation(nvs, ssid, password);
        loadApPassword(nvs, apPassword_);
        if (!forced) wifiPower_ = savedWifiPower(nvs);
        if (nvs) nvs_close(nvs);
        if (!networkName(name_) || esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK) return false;
        if (!station && handoff_.mode == Mode::Station) handoff_ = {};
        mode_ = station ? Mode::Station : Mode::AccessPoint;
        if (!startWifi(ssid, password)) return false;
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.lru_purge_enable = true;
        config.recv_wait_timeout = 4;
        config.stack_size = 6144;
        if (httpd_start(&server_, &config) != ESP_OK) return false;
        return route("/", HTTP_GET) && route("/update/status", HTTP_GET) &&
               route("/firmware", HTTP_POST) && route("/boot", HTTP_POST);
    }
    void recoverFromStartupFailure() {
        if (handoff_.mode != Mode::Off && ota::selectApplication()) esp_restart();
    }
    [[noreturn]] void run() {
        const auto started = xTaskGetTickCount();
        bool waiting = handoff_.mode != Mode::Off;
        for (;;) {
            Watchdog::feed();
            if (waiting && xTaskGetTickCount() - started >= WebTimeout && !busy_.exchange(true)) {
                // A client that could not reconnect must not leave a working application stranded.
                waiting = false;
                if (ota::selectApplication()) ota::restartOnNetwork();
                if (pinnedStation_) esp_netif_dhcpc_start(pinnedStation_);
                busy_.store(false);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
private:
    static void stationDisconnected(void *, esp_event_base_t, int32_t, void *) { esp_wifi_connect(); }
    bool startWifi(const char *ssid, const char *password) {
        const bool sta = mode_ == Mode::Station;
        auto *iface = sta ? esp_netif_create_default_wifi_sta() : esp_netif_create_default_wifi_ap();
        if (!iface) return false;
        wifi_init_config_t defaults = WIFI_INIT_CONFIG_DEFAULT();
        wifi_config_t wifi{};
        if (sta) {
            // Keep the browser's address during the web handoff. After power loss, obtain a fresh lease.
            if (esp_reset_reason() == ESP_RST_SW) {
                const auto stopped = esp_netif_dhcpc_stop(iface);
                if ((stopped != ESP_OK && stopped != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) ||
                    esp_netif_set_ip_info(iface, &handoff_.address) != ESP_OK) return false;
                pinnedStation_ = iface;
            }
            if (esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, stationDisconnected, nullptr) != ESP_OK) return false;
            stationSettings(wifi, ssid, password);
        } else accessPointSettings(wifi, name_, apPassword_);
        if (esp_wifi_init(&defaults) != ESP_OK || esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
            esp_wifi_set_mode(sta ? WIFI_MODE_STA : WIFI_MODE_AP) != ESP_OK ||
            esp_wifi_set_config(sta ? WIFI_IF_STA : WIFI_IF_AP, &wifi) != ESP_OK ||
            esp_wifi_start() != ESP_OK || esp_wifi_set_max_tx_power(wifiPower_) != ESP_OK ||
            (sta && esp_wifi_connect() != ESP_OK)) return false;
        if (!sta) std::printf("Recovery Wi-Fi: %s\nWi-Fi password: %s\nOpen http://192.168.4.1/\n",
                             name_, apPassword_);
        return true;
    }
    static esp_err_t dispatch(httpd_req_t *req) { return static_cast<Recovery *>(req->user_ctx)->handle(req); }
    bool route(const char *uri, httpd_method_t method) {
        httpd_uri_t value{};
        value.uri = uri; value.method = method; value.handler = dispatch; value.user_ctx = this;
        return httpd_register_uri_handler(server_, &value) == ESP_OK;
    }
    esp_err_t finish(httpd_req_t *req, const char *message) {
        if (handoff_.mode == Mode::Off) {
            ota::WebNetwork active;
            active.mode = mode_;
            if (!ota::saveWebNetwork(active))
                return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not preserve the current network.");
        }
        if (!ota::selectApplication())
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No valid companion firmware. Upload the companion .bin.");
        httpd_resp_sendstr(req, message);
        ota::restartOnNetwork();
    }
    esp_err_t handle(httpd_req_t *req) {
        if (req->method == HTTP_GET)
            return std::strcmp(req->uri, "/") ? ota::updateStatus(req, "recovery") : recoveryPage(req);
        if (!httpSameOrigin(req)) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid origin.");
        if (busy_.exchange(true)) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Update already in progress.");
        const bool upload = !std::strcmp(req->uri, "/firmware");
        const auto result = upload && ota::uploadApplication(req) != ESP_OK ? ESP_FAIL :
            finish(req, upload ? "Firmware verified and installed. Restarting the companion." : "Starting the installed companion firmware.");
        busy_.store(false);
        return result;
    }
    ota::WebNetwork handoff_{};
    Mode mode_ = Mode::Off;
    uint8_t wifiPower_ = DefaultWifiPower;
    char name_[ApNameBytes + 1]{}, apPassword_[PasswordBytes + 1]{};
    httpd_handle_t server_ = nullptr;
    esp_netif_t *pinnedStation_ = nullptr;
    std::atomic<bool> busy_{false};
};
}
}

extern "C" void app_main() {
    using namespace openosc::companion;
    Watchdog watchdog;
    if (!watchdog.registered()) esp_restart();
    const bool settingsReady = nvs_flash_init() == ESP_OK;
    static Recovery recovery;
    if (!recovery.start(settingsReady)) {
        recovery.recoverFromStartupFailure();
        esp_restart();
    }
    recovery.run();
}
