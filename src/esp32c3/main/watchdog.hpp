#pragma once
#include "esp_task_wdt.h"

namespace openosc::companion {
class Watchdog {
public:
    Watchdog() : registered_(esp_task_wdt_add(nullptr) == ESP_OK) {}
    ~Watchdog() { if (registered_) esp_task_wdt_delete(nullptr); }
    Watchdog(const Watchdog &) = delete;
    Watchdog &operator=(const Watchdog &) = delete;
    bool registered() const { return registered_; }
    static void feed() { esp_task_wdt_reset(); }
private:
    bool registered_;
};
}
