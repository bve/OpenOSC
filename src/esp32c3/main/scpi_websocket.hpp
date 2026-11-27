#pragma once

#include "bridge.hpp"
#include "esp_http_server.h"

namespace openosc::companion {
class ScpiWebSocket {
public:
    explicit ScpiWebSocket(Bridge &bridge) : bridge_(bridge) {}
    void poll(uint32_t now, uint32_t ipv4);
    void stop();

private:
    bool start();
    static esp_err_t handshake(httpd_req_t *req);
    static esp_err_t receive(httpd_req_t *req);
    static void disconnect(httpd_handle_t server, int fd);
    static void drain(void *context);

    Bridge &bridge_;
    httpd_handle_t server_ = nullptr;
    std::atomic<int> client_{-1};
    std::atomic<bool> workQueued_{false};
    uint32_t ipv4_ = 0, retryAt_ = 0;
    bool fragmented_ = false;
    uint8_t input_[1024]{};
};
}
