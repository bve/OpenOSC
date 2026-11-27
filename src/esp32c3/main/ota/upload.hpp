#pragma once
#include "esp_http_server.h"

namespace openosc::companion::ota {
bool applicationProbe(httpd_req_t *req);
esp_err_t uploadApplication(httpd_req_t *req);
esp_err_t uploadRecovery(httpd_req_t *req);
}
