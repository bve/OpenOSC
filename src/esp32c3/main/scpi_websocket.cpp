#include "scpi_websocket.hpp"
#include <cstdio>
#include <cstring>
#include "esp_timer.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

namespace openosc::companion {
namespace {
using Owner = Bridge::Owner;
constexpr auto ScpiOwner = Owner::WebSocket;
constexpr uint16_t WebSocketPort = 5026;
constexpr uint32_t InputTimeoutMs = 2000;

uint32_t milliseconds() { return esp_timer_get_time() / 1000; }

bool allowedOrigin(httpd_req_t *req, uint32_t ipv4) {
    if (!httpd_req_get_hdr_value_len(req, "Origin")) return true;
    char origin[80], address[INET_ADDRSTRLEN], adminOrigin[sizeof "http://" + INET_ADDRSTRLEN];
    if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof origin) != ESP_OK) return false;
    if (!std::strcmp(origin, "https://fyfar.github.io")) return true;
    const in_addr ip{htonl(ipv4)};
    if (!inet_ntop(AF_INET, &ip, address, sizeof address)) return false;
    std::snprintf(adminOrigin, sizeof adminOrigin, "http://%s", address);
    return !std::strcmp(origin, adminOrigin);
}

esp_err_t reject(httpd_req_t *req, const char *status, const char *reason) {
    httpd_resp_set_status(req, status);
    httpd_resp_sendstr(req, reason);
    return ESP_FAIL;
}

void sendClose(httpd_handle_t server, int fd, uint16_t code) {
    uint8_t bytes[] = {uint8_t(code >> 8), uint8_t(code)};
    httpd_ws_frame_t frame{};
    frame.type = HTTPD_WS_TYPE_CLOSE;
    frame.payload = bytes;
    frame.len = sizeof bytes;
    httpd_ws_send_frame_async(server, fd, &frame);
}

int sendComplete(httpd_handle_t, int fd, const char *data, size_t size, int flags) {
    const int sent = send(fd, data, size, flags);
    // The WebSocket frame writer handles errors but does not retry short sends.
    return sent >= 0 && size_t(sent) == size ? sent : HTTPD_SOCK_ERR_FAIL;
}
}

bool ScpiWebSocket::start() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WebSocketPort;
    config.ctrl_port = 32769;
    config.max_open_sockets = 2;
    config.max_uri_handlers = 1;
    config.recv_wait_timeout = config.send_wait_timeout = 1;
    config.keep_alive_enable = true;
    config.keep_alive_idle = 15;
    config.keep_alive_interval = 5;
    config.keep_alive_count = 3;
    config.global_user_ctx = this;
    config.global_user_ctx_free_fn = [](void *) {};
    config.close_fn = disconnect;
    if (httpd_start(&server_, &config) != ESP_OK) return false;

    httpd_uri_t route{};
    route.uri = "/scpi";
    route.method = HTTP_GET;
    route.handler = receive;
    route.user_ctx = this;
    route.is_websocket = true;
    route.handle_ws_control_frames = true;
    route.ws_pre_handshake_cb = handshake;
    if (httpd_register_uri_handler(server_, &route) == ESP_OK) return true;
    stop();
    return false;
}

void ScpiWebSocket::stop() {
    if (server_) {
        if (httpd_stop(server_) != ESP_OK) return;
        server_ = nullptr;
    }
    workQueued_ = false;
    retryAt_ = 0;
}

void ScpiWebSocket::poll(uint32_t now, uint32_t ipv4) {
    if (ipv4 != ipv4_) {
        stop();
        if (server_) return;
        ipv4_ = ipv4;
    }
    if (!server_) {
        if (retryAt_ && int32_t(now - retryAt_) < 0) return;
        if (!start()) {
            retryAt_ = now + 1000;
            return;
        }
    }
    if (client_.load() < 0 || workQueued_.load()) return;
    if (bridge_.active(ScpiOwner) && !bridge_.hasOutput(ScpiOwner)) return;
    workQueued_ = true;
    if (httpd_queue_work(server_, drain, this) != ESP_OK) workQueued_ = false;
}

esp_err_t ScpiWebSocket::handshake(httpd_req_t *req) {
    auto &service = *static_cast<ScpiWebSocket *>(req->user_ctx);
    if (!allowedOrigin(req, service.ipv4_))
        return reject(req, "403 Forbidden", "SCPI WebSocket origin is not allowed.");
    if (service.client_.load() >= 0 || !service.bridge_.open(ScpiOwner, milliseconds()))
        return reject(req, "409 Conflict", "Another wireless SCPI client is connected.");
    const int fd = httpd_req_to_sockfd(req);
    httpd_sess_set_send_override(req->handle, fd, sendComplete);
    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof yes);
    service.fragmented_ = false;
    service.client_ = fd;
    return ESP_OK;
}

esp_err_t ScpiWebSocket::receive(httpd_req_t *req) {
    const int fd = httpd_req_to_sockfd(req);
    if (req->method == HTTP_GET && req->uri[0]) {
        if (httpd_ws_get_fd_info(req->handle, fd) == HTTPD_WS_CLIENT_WEBSOCKET) return ESP_OK;
        httpd_resp_set_hdr(req, "Upgrade", "websocket");
        return reject(req, "426 Upgrade Required", "Connect with a WebSocket client.");
    }
    auto &service = *static_cast<ScpiWebSocket *>(req->user_ctx);
    httpd_ws_frame_t frame{};
    if (httpd_ws_recv_frame(req, &frame, 0) != ESP_OK) return ESP_FAIL;
    const auto closeWith = [&](uint16_t code) {
        sendClose(req->handle, fd, code);
        return ESP_FAIL;
    };
    if (frame.len > sizeof service.input_) return closeWith(1009);
    if (frame.type >= HTTPD_WS_TYPE_CLOSE && (!frame.final || frame.len > 125)) return closeWith(1002);
    frame.payload = service.input_;
    if (frame.len && httpd_ws_recv_frame(req, &frame, sizeof service.input_) != ESP_OK) return ESP_FAIL;
    switch (frame.type) {
    case HTTPD_WS_TYPE_CLOSE:
        if (frame.len == 1) return closeWith(1002);
        httpd_ws_send_frame(req, &frame);
        return ESP_FAIL;
    case HTTPD_WS_TYPE_PING:
        frame.type = HTTPD_WS_TYPE_PONG;
        return httpd_ws_send_frame(req, &frame);
    case HTTPD_WS_TYPE_PONG:
        return ESP_OK;
    case HTTPD_WS_TYPE_CONTINUE:
    case HTTPD_WS_TYPE_TEXT:
    case HTTPD_WS_TYPE_BINARY:
        if ((frame.type == HTTPD_WS_TYPE_CONTINUE) != service.fragmented_) return closeWith(1002);
        service.fragmented_ = !frame.final;
        break;
    default:
        return closeWith(1002);
    }
    const uint32_t start = milliseconds();
    size_t used = 0;
    while (used < frame.len) {
        const auto now = milliseconds();
        if (!service.bridge_.active(ScpiOwner) || now - start >= InputTimeoutMs) return closeWith(1013);
        used += service.bridge_.input(ScpiOwner, frame.payload + used, frame.len - used, now);
        if (used < frame.len) vTaskDelay(pdMS_TO_TICKS(2));
    }
    return ESP_OK;
}

void ScpiWebSocket::disconnect(httpd_handle_t server, int fd) {
    auto &service = *static_cast<ScpiWebSocket *>(httpd_get_global_user_ctx(server));
    if (service.client_.load() == fd) {
        service.bridge_.close(ScpiOwner);
        service.client_ = -1;
    }
    close(fd);
}

void ScpiWebSocket::drain(void *context) {
    auto &service = *static_cast<ScpiWebSocket *>(context);
    const int fd = service.client_.load();
    if (fd >= 0 && httpd_ws_get_fd_info(service.server_, fd) == HTTPD_WS_CLIENT_WEBSOCKET) {
        if (service.bridge_.active(ScpiOwner)) {
            uint8_t bytes[512];
            httpd_ws_frame_t frame{};
            frame.type = HTTPD_WS_TYPE_BINARY;
            frame.payload = bytes;
            frame.len = service.bridge_.output(ScpiOwner, bytes, sizeof bytes);
            if (frame.len) {
                if (httpd_ws_send_frame_async(service.server_, fd, &frame) == ESP_OK)
                    service.bridge_.sent(ScpiOwner, frame.len, milliseconds());
                else
                    shutdown(fd, SHUT_RDWR);
            }
        } else {
            sendClose(service.server_, fd, 1013);
            shutdown(fd, SHUT_RDWR);
        }
    }
    service.workQueued_ = false;
}
}
