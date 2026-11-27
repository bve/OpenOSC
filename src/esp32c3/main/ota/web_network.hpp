#pragma once
#include "esp_netif.h"
#include "openosc/companion/protocol.hpp"

namespace openosc::companion::ota {
struct WebNetwork {
    Mode mode = Mode::Off;
    esp_netif_ip_info_t address{};
};
WebNetwork loadWebNetwork();
bool saveWebNetwork(const WebNetwork &network);
bool clearWebNetwork();
bool captureWebNetwork(WebNetwork &network);
[[noreturn]] void restartOnNetwork();
}
