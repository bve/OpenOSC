#pragma once
#include "openosc/companion/wifi_power.hpp"
#include "esp_wifi.h"
#include "file_page.hpp"
#include "ota/web_page.hpp"
#include "web_layout.hpp"

namespace openosc::companion::web {
inline constexpr char SettingsStart[] = R"html(
<nav aria-label="Settings sections">
<a href="#network">Wi-Fi</a><a href="#services">Services</a><a href="#files">Files</a><a href="#firmware">ESP32-C3 Update</a>
</nav>
<main>
<section id="network" aria-labelledby="network-heading">
<h2 id="network-heading">Wi-Fi</h2>
<div class="panel">
<form class="row" method="post" action="/mode">
<h3>Mode</h3>
<div class="fields"><label>Connection mode <select name="mode">
)html";
inline constexpr char PowerStart[] = R"html(
</select></label></div>
<button>Apply mode</button>
</form>
<form class="row" method="post" action="/join">
<div><h3>Join a network</h3><p class="hint">Connects as a station.</p></div>
<div class="fields">
<label>Network name <input name="ssid" maxlength="32" autocomplete="off" autocapitalize="off" spellcheck="false" required></label>
<label>Wi-Fi password <input name="password" type="password" maxlength="63"></label>
</div>
<button>Connect</button>
</form>
<form class="row" method="post" action="/ap-password">
<div><h3>Access point</h3><p class="hint">Reconnect with the new password after saving.</p></div>
<div class="fields"><label>Access point password <input name="password" type="password" autocomplete="new-password" minlength="8" maxlength="63" required></label></div>
<button>Save password</button>
</form>
<form class="row" method="post" action="/tx-power">
<h3>Transmit power</h3>
<div class="fields"><label>Wi-Fi TX power limit <select name="power">
)html";
inline constexpr char SettingsRest[] = R"html(
</select></label></div>
<button>Apply TX power</button>
</form>
</div>
</section>
<section id="services" aria-labelledby="services-heading">
<h2 id="services-heading">Services</h2>
<div class="panel">
<form class="row" method="post" action="/services">
<div><h3>Remote control</h3><p class="hint">Keep current leaves a setting unchanged.</p></div>
<div class="fields">
<label>Wi-Fi SCPI (TCP 5025 / WebSocket 5026)
<select name="tcp"><option value="255">Keep current</option><option value="0">Off</option><option value="1">On</option></select>
</label>
<label>Bluetooth LE SCPI
<select name="ble"><option value="255">Keep current</option><option value="0">Off</option><option value="1">On</option></select>
</label>
<label>Sleep after inactivity
<select name="idle"><option value="255">Keep current</option><option value="0">Never</option><option value="1">1 min</option><option value="5">5 min</option><option value="15">15 min</option><option value="30">30 min</option></select>
</label>
</div>
<button>Apply services</button>
</form>
<form class="row" method="post" action="/time" onsubmit="this.epoch.value=Math.floor(Date.now()/1000)">
<div><h3>Time</h3><p class="hint">Set the module clock from this browser.</p></div>
<input type="hidden" name="epoch">
<button>Sync time</button>
</form>
</div>
</section>
<section id="firmware" aria-labelledby="firmware-heading">
<h2 id="firmware-heading">ESP32-C3 Update</h2>
<p class="hint update-target">Update the ESP32-C3 Wireless Module here. To update the instrument, use OpenOSC Recovery on the instrument.</p>
)html";

inline esp_err_t option(httpd_req_t *req, unsigned value, bool selected, const char *label, const char *unit = "") {
    char text[84];
    const int size = std::snprintf(text, sizeof text, "<option value=\"%u\"%s>%s%s</option>",
                                  value, selected ? " selected" : "", label, unit);
    if (size < 0 || size >= int(sizeof text)) return ESP_FAIL;
    return httpd_resp_sendstr_chunk(req, text);
}

inline esp_err_t settings(httpd_req_t *req, uint8_t power) {
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    if (begin(req) != ESP_OK || httpd_resp_sendstr_chunk(req, SettingsStart) != ESP_OK ||
        option(req, 2, mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA, "Access point") != ESP_OK ||
        option(req, 1, mode == WIFI_MODE_STA, "Station") != ESP_OK ||
        option(req, 0, mode == WIFI_MODE_NULL, "Off") != ESP_OK ||
        httpd_resp_sendstr_chunk(req, PowerStart) != ESP_OK) return ESP_FAIL;
    for (const auto &level : WifiPowerLevels) {
        if (option(req, level.quarters, level.quarters == power, level.dbm, " dBm") != ESP_OK) return ESP_FAIL;
    }
    if (httpd_resp_sendstr_chunk(req, SettingsRest) != ESP_OK || ota::updateControls(req, true) != ESP_OK ||
        httpd_resp_sendstr_chunk(req, "</section>") != ESP_OK ||
        httpd_resp_sendstr_chunk(req, FilesPage) != ESP_OK ||
        httpd_resp_sendstr_chunk(req, "</main>") != ESP_OK ||
        httpd_resp_sendstr_chunk(req, FilesScript) != ESP_OK ||
        httpd_resp_sendstr_chunk(req, NavigationScript) != ESP_OK) return ESP_FAIL;
    return end(req);
}
}
