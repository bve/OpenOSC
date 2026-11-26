#pragma once
#include <cstddef>
#include <cstdint>

namespace openosc::scope { class AcquisitionArena; }
struct z_stream_s; // zlib.h stays out of the headers that include this (zconf's FAR, the STM32 RAMECC).

namespace openosc {
// zlib's memory in the 16 KiB AcquisitionArena::Trial area, shared by PNG screenshots and the waveform
// gzip exports (GitHub #166). claim() is the foreground's, refused while a trial capture's DMA or a
// trial frame on show holds the area (or another user); allocate() bumps through it, aligned, and
// release() returns it as a whole (zfree does nothing).
class ZlibArena {
public:
    ZlibArena() = default;
    ZlibArena(const ZlibArena &) = delete;
    ZlibArena &operator=(const ZlibArena &) = delete;
    bool claim(scope::AcquisitionArena &arena);
    void release();
    bool claimed() const { return arena_ != nullptr; }
    void attach(z_stream_s &stream); // Its zalloc, zfree and opaque.
    uint8_t *allocate(size_t bytes);
    size_t used() const { return used_; }

private:
    static void *allocate(void *context, unsigned count, unsigned size);
    static void free(void *, void *) {}
    scope::AcquisitionArena *arena_ = nullptr;
    size_t used_ = 0;
};
} // namespace openosc
