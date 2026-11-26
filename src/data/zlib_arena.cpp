#include "openosc/data/zlib_arena.hpp"
#include "openosc/acquisition/arena.hpp"
#include <zlib.h>

namespace openosc {
bool ZlibArena::claim(scope::AcquisitionArena &arena) {
    if (arena_ || !arena.claim(scope::AcquisitionArena::Trial)) return false;
    arena_ = &arena;
    used_ = 0;
    return true;
}
void ZlibArena::release() {
    if (!arena_) return;
    arena_->release(scope::AcquisitionArena::Trial);
    arena_ = nullptr;
}
void ZlibArena::attach(z_stream_s &stream) {
    stream.opaque = this;
    stream.zalloc = &allocate;
    stream.zfree = &free;
}
void *ZlibArena::allocate(void *context, unsigned count, unsigned size) {
    if (!size || count > scope::AcquisitionArena::TrialBytes / size) return nullptr;
    return static_cast<ZlibArena *>(context)->allocate(size_t(count) * size);
}
uint8_t *ZlibArena::allocate(size_t bytes) {
    constexpr size_t Alignment = alignof(std::max_align_t);
    const size_t offset = (used_ + Alignment - 1) & ~(Alignment - 1);
    if (!arena_ || offset > scope::AcquisitionArena::TrialBytes || bytes > scope::AcquisitionArena::TrialBytes - offset)
        return nullptr;
    used_ = offset + bytes;
    return reinterpret_cast<uint8_t *>(scope::AcquisitionArena::trialMemory()) + offset;
}
} // namespace openosc
