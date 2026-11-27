#include "http_origin.hpp"
#include <cstdio>
#include <cstring>

namespace openosc::companion {
bool httpSameOrigin(httpd_req_t *req) {
    if (!httpd_req_get_hdr_value_len(req, "Origin")) return true;
    char origin[80], host[70], expected[80];
    if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof origin) != ESP_OK ||
        httpd_req_get_hdr_value_str(req, "Host", host, sizeof host) != ESP_OK) return false;
    const int size = std::snprintf(expected, sizeof expected, "http://%s", host);
    return size >= 0 && size < int(sizeof expected) && !std::strcmp(origin, expected);
}
}
