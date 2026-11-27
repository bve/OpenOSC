#include "local_name.hpp"
#include "esp_log.h"
#include "mdns.h"
#include <cstdio>
#include <strings.h>

namespace openosc::companion {
bool LocalName::matchesHttpOrigin(const char *origin) {
    char expected[sizeof Host + sizeof "http://.local"];
    std::snprintf(expected, sizeof expected, "http://%s.local", Host);
    return strcasecmp(origin, expected) == 0;
}

void LocalName::start() {
    if (running_) return;
    const auto result = mdns_init();
    if (result != ESP_OK) {
        ESP_LOGW("local_name", "mDNS startup failed: %s", esp_err_to_name(result));
        return;
    }
    running_ = true;
    const auto named = mdns_hostname_set(Host);
    if (named != ESP_OK) {
        ESP_LOGW("local_name", "Cannot advertise %s.local: %s", Host, esp_err_to_name(named));
        stop();
    }
}

void LocalName::stop() {
    if (!running_) return;
    mdns_free();
    running_ = false;
}
}
