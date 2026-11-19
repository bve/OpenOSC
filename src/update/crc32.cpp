#include "openosc/update/crc32.hpp"

namespace openosc {
namespace {
struct Table {
    uint32_t entry[256];
    constexpr Table() : entry() {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (unsigned bit = 0; bit < 8; ++bit) c = (c >> 1) ^ ((c & 1) ? 0xedb88320U : 0U);
            entry[i] = c;
        }
    }
};
constexpr Table table; // A byte a step: the boot's image checks were 8 shifts a byte.
}
void Crc32::update(const void* data, size_t size) {
    auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t c = state_;
    while (size--) c = (c >> 8) ^ table.entry[(c ^ *bytes++) & 0xffU];
    state_ = c;
}
uint32_t Crc32::compute(const void* data, size_t size) {
    Crc32 crc;
    crc.update(data, size);
    return crc.value();
}
}

