#pragma once
#include <cstddef>
#include <cstdint>

namespace openosc::companion {
class DapProbe {
public:
    static constexpr size_t PacketSize = 512;
    void reserve();
    void disconnect();
    bool detachTarget();
    void release();
    size_t execute(uint8_t *request, size_t size, uint8_t *response);
private:
    bool usedDebugPort_ = false;
};
}
