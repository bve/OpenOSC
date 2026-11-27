#pragma once
#include "esp_http_server.h"

namespace openosc::companion {
bool httpSameOrigin(httpd_req_t *req);
}
