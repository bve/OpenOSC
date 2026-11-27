#pragma once
#include "esp_http_server.h"
#include "file_mailbox.hpp"

namespace openosc::companion {
esp_err_t serveFiles(httpd_req_t *req, FileMailbox &mailbox);
}
