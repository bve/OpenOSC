#pragma once

namespace openosc::companion {
class LocalName {
public:
    static constexpr char Host[] = "openosc";
    static bool matchesHttpOrigin(const char *origin);
    void start();
    void stop();

private:
    bool running_ = false;
};
}
