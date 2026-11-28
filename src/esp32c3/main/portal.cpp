#include "portal.hpp"
#include "http_origin.hpp"
#include "http_fields.hpp"
#include "file_portal.hpp"
#include "network_identity.hpp"
#include "ota/boot.hpp"
#include "ota/upload.hpp"
#include "ota/web_network.hpp"
#include "portal_page.hpp"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include <cstring>
#include <iterator>

namespace openosc::companion {
using web::field;
using web::number;
esp_err_t Portal::connect(httpd_handle_t server, int) {
    ++static_cast<Portal *>(httpd_get_global_user_ctx(server))->clients_;
    return ESP_OK;
}
void Portal::disconnect(httpd_handle_t server, int socket) {
    --static_cast<Portal *>(httpd_get_global_user_ctx(server))->clients_;
    close(socket);
}
esp_err_t Portal::request(httpd_req_t *req) { return static_cast<Portal *>(req->user_ctx)->serve(req); }
esp_err_t Portal::redirect(httpd_req_t *req, httpd_err_code_t) {
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK && (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
        return httpd_resp_send(req, nullptr, 0);
    }
    return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Page not found");
}
esp_err_t Portal::serve(httpd_req_t *req) {
    if (req->method == HTTP_GET)
        return std::strcmp(req->uri, "/") ? ota::updateStatus(req, "application") : web::settings(req, network_.wifiPower());
    if (!httpSameOrigin(req)) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid origin");
    if (!std::strcmp(req->uri, "/files")) return serveFiles(req, files_);
    if (!std::strcmp(req->uri, "/update")) return update(req, false);
    if (!std::strcmp(req->uri, "/update/recovery")) return update(req, true);
    char form[384];
    if (!req->content_len || req->content_len >= sizeof form)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form size");
    size_t size = 0;
    while (size < req->content_len) {
        const int got = httpd_req_recv(req, form + size, req->content_len - size);
        if (got <= 0) return ESP_FAIL;
        size += got;
    }
    form[size] = 0;
    PortalRequest command{};
    uint32_t a = 0, b = 0, c = 0;
    bool valid = false;
    if (!std::strcmp(req->uri, "/mode")) {
        command.action = PortalRequest::Action::Mode;
        valid = number(form, "mode", a, 2);
        command.mode = a;
    } else if (!std::strcmp(req->uri, "/join")) {
        command.action = PortalRequest::Action::Join;
        valid = field(form, "ssid", command.ssid, sizeof command.ssid) && *command.ssid &&
                field(form, "password", command.password, sizeof command.password) &&
                (!*command.password || std::strlen(command.password) >= 8);
    } else if (!std::strcmp(req->uri, "/services")) {
        command.action = PortalRequest::Action::Services;
        valid = number(form, "tcp", a, UINT8_MAX) && (a <= 1 || a == UINT8_MAX) &&
                number(form, "ble", b, UINT8_MAX) && (b <= 1 || b == UINT8_MAX) &&
                number(form, "idle", c, UINT8_MAX) && (c == UINT8_MAX || idleValid(c));
        command.tcp = a; command.ble = b; command.idle = c;
    } else if (!std::strcmp(req->uri, "/tx-power")) {
        command.action = PortalRequest::Action::WifiPower;
        valid = number(form, "power", a, UINT8_MAX) && wifiPowerValid(a);
        command.power = a;
    } else if (!std::strcmp(req->uri, "/ap-password")) {
        command.action = PortalRequest::Action::ApPassword;
        valid = field(form, "password", command.password, sizeof command.password) && validApPassword(command.password);
    } else if (!std::strcmp(req->uri, "/time")) {
        command.action = PortalRequest::Action::Time;
        valid = number(form, "epoch", command.epoch, UINT32_MAX);
    }
    // HTTP handlers run serially and are the only producers. The main loop can only free queue space.
    if (!valid || !network_.canSubmit()) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Settings could not be queued");
    using Action = PortalRequest::Action;
    const bool apPassword = command.action == Action::ApPassword;
    const bool services = command.action == Action::Services || command.action == Action::Time;
    const char *message = apPassword ? "Reconnect with the new password when using the access point."
        : command.action == Action::Join || command.action == Action::Mode
            ? "Reconnect to the selected network if this page becomes unavailable."
            : "Return to settings to see the current values.";
    const auto sent = web::accepted(req, message, services ? "services" : "network");
    if (sent == ESP_OK) {
        // Deliver the response before changing the radio settings.
        if (apPassword || command.action == Action::Join || command.action == Action::Mode || command.action == Action::WifiPower)
            vTaskDelay(pdMS_TO_TICKS(300));
        network_.submit(command);
    }
    return sent;
}

esp_err_t Portal::update(httpd_req_t *req, bool recovery) {
    updating_.store(true);
    const struct ClearActivity {
        std::atomic<bool> &active;
        ~ClearActivity() { active.store(false); }
    } activity{updating_};
    if (recovery) {
        if (ota::uploadRecovery(req) != ESP_OK) return ESP_FAIL;
        return httpd_resp_sendstr(req, "Recovery verified and installed.");
    }
    if (!ota::applicationProbe(req))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Select the unmerged ESP32-C3 companion application .bin.");
    if (!ota::webRecoveryAvailable())
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Update Recovery from this page before updating the companion.");
    ota::WebNetwork network;
    if (!ota::captureWebNetwork(network) || !ota::saveWebNetwork(network))
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not preserve the current network.");
    if (!ota::selectRecovery()) {
        ota::clearWebNetwork();
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Recovery image unavailable.");
    }
    httpd_resp_sendstr(req, "Restarting in update mode.");
    ota::restartOnNetwork();
}

void Portal::poll(bool up) {
    if (!up) { stop(); return; }
    if (server_) return;
    static constexpr const char *Paths[] = {
        "/", "/mode", "/join", "/services", "/time", "/tx-power", "/ap-password", "/files", "/update", "/update/recovery", "/update/status"
    };
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = std::size(Paths);
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 4;
    config.stack_size = 6144;
    config.keep_alive_enable = true;
    config.keep_alive_idle = 15;
    config.keep_alive_interval = 5;
    config.keep_alive_count = 3;
    config.global_user_ctx = this;
    config.global_user_ctx_free_fn = [](void *) {};
    config.open_fn = connect;
    config.close_fn = disconnect;
    if (httpd_start(&server_, &config) != ESP_OK) return;
    for (const char *path : Paths) {
        httpd_uri_t route{};
        const bool read = !std::strcmp(path, "/") || !std::strcmp(path, "/update/status");
        route.uri = path; route.method = read ? HTTP_GET : HTTP_POST;
        route.handler = request; route.user_ctx = this;
        if (httpd_register_uri_handler(server_, &route) != ESP_OK) { stop(); return; }
    }
    httpd_register_err_handler(server_, HTTPD_404_NOT_FOUND, redirect);
    files_.online(true);
}
void Portal::stop() {
    if (server_) {
        files_.online(false);
        httpd_stop(server_);
        server_ = nullptr;
    }
}
}
