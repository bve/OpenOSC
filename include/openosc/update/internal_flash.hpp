#pragma once
#include <cstddef>
#include <cstdint>

namespace openosc {
class InternalFlash {
public:
    // Opt-in for one caller: lets erase and program reach layout::FactoryBootSector, nothing else below the application.
    void allowFactoryBootSector() { factoryBootSector_ = true; }
    bool eraseSector(uint32_t address);
    bool program(uint32_t address, const void* data, size_t size);
    void lock();
    const char* error() const { return error_; }
private:
    bool writable(uint32_t address, size_t size) const;
    bool unlock(uint32_t address);
    bool wait(uint32_t address);
    bool fail(const char* message);
    const char* error_ = nullptr;
    bool factoryBootSector_ = false;
};
}

