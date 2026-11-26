#pragma once
#include "openosc/screenshot/bitmap_layout.hpp"
#include "openosc/update/crc32.hpp"
#include "ff.h"
#include <zlib.h>
#undef FAR // zlib's 16-bit pointer qualifier collides with the STM32 RAMECC register.

namespace openosc::scope { class AcquisitionArena; }

namespace openosc::screenshot {
namespace png {
constexpr size_t SignatureBytes=8, HeaderBytes=33, ChunkOverhead=12;
constexpr unsigned RowBytes=BitmapLayout::Width*3, ScanlineBytes=RowBytes+1;
constexpr uint32_t IHDR=0x49484452, IDAT=0x49444154, IEND=0x49454e44;
bool signature(const uint8_t *bytes, size_t size);
void header(uint8_t *bytes);
bool readHeader(FIL &file);
uint32_t readWord(const uint8_t *bytes);
void writeWord(uint8_t *bytes, uint32_t word);
// The caller has already placed the payload at chunk+8.
void sealChunk(uint8_t *chunk, uint32_t type, size_t payload);
}

// Screenshot encoding and viewing borrow the same 16 KiB arena as acquisition
// trials. The foreground claim excludes DMA and any displayed trial frame.
class PngStream {
public:
    bool begin(scope::AcquisitionArena &arena, bool encode);
    void end();
    z_stream &io() { return io_; }
    uint8_t *scanline() const { return scanline_; }
    uint8_t *allocate(size_t bytes);

private:
    static voidpf allocate(voidpf context, unsigned count, unsigned size);
    static void free(voidpf, voidpf) {} // The arena is returned as a whole.
    z_stream io_{};
    scope::AcquisitionArena *arena_=nullptr;
    uint8_t *scanline_=nullptr;
    size_t used_=0;
    bool encode_=false;
};

class PngEncoder {
public:
    bool start(scope::AcquisitionArena &arena, const uint16_t *pixels);
    // May return zero bytes while building the next IDAT chunk.
    bool service(uint8_t *out, size_t capacity, size_t &bytes);
    void cancel() { stream_.end(); }
    bool complete() const { return complete_; }
    unsigned rows() const { return row_; }

private:
    void prepareRow();
    PngStream stream_;
    const uint16_t *pixels_=nullptr;
    unsigned row_=0;
    size_t prefix_=0;
    bool first_=true, chunkActive_=false, ended_=false, complete_=false;
};

class PngReader {
public:
    bool start(FIL &file, scope::AcquisitionArena &arena);
    bool service(FIL &file, uint8_t *previous, uint16_t *preview, bool fullSize);
    void cancel() { stream_.end(); }
    bool complete() const { return complete_; }
    unsigned rows() const { return row_; }

private:
    bool refill(FIL &file);
    bool commitRow(uint8_t *previous, uint16_t *preview, bool fullSize);
    PngStream stream_;
    Crc32 crc_;
    uint8_t *input_=nullptr;
    uint32_t remaining_=0;
    unsigned row_=0, previewRow_=0;
    bool seenData_=false, drain_=false, ended_=false, complete_=false;
};
}
