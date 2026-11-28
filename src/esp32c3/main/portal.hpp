#pragma once
#include "network.hpp"
#include "file_mailbox.hpp"
#include "esp_http_server.h"

namespace openosc::companion {
class Portal {
public:
    Portal(Network &network, FileMailbox &files) : network_(network), files_(files) {}
    void poll(bool up);
    void stop();
    bool updating() const { return updating_.load(); }
    bool hasClients() const { return clients_.load() != 0; }
private:
    static esp_err_t connect(httpd_handle_t server, int);
    static void disconnect(httpd_handle_t server, int socket);
    static esp_err_t request(httpd_req_t *request);
    static esp_err_t redirect(httpd_req_t *request, httpd_err_code_t);
    esp_err_t serve(httpd_req_t *request);
    esp_err_t update(httpd_req_t *request, bool recovery);
    Network &network_;
    FileMailbox &files_;
    httpd_handle_t server_ = nullptr;
    std::atomic<bool> updating_{false};
    std::atomic<unsigned> clients_{0};
};
}
