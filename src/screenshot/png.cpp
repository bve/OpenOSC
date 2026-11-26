#include "openosc/screenshot/png.hpp"
#include "openosc/acquisition/arena.hpp"
#include <cstddef>
#include <cstring>

namespace openosc::screenshot {
namespace png {
namespace {
constexpr uint8_t Signature[]{137, 'P', 'N', 'G', 13, 10, 26, 10};
constexpr uint8_t Rgb8[]{8, 2, 0, 0, 0}; // Depth, color, compression, filter, interlace.
}

bool signature(const uint8_t *bytes, size_t size) {
    return size>=SignatureBytes && std::memcmp(bytes,Signature,SignatureBytes)==0;
}

uint32_t readWord(const uint8_t *bytes) {
    return uint32_t(bytes[0])<<24 | uint32_t(bytes[1])<<16 | uint32_t(bytes[2])<<8 | bytes[3];
}

void writeWord(uint8_t *bytes, uint32_t word) {
    bytes[0]=uint8_t(word>>24);
    bytes[1]=uint8_t(word>>16);
    bytes[2]=uint8_t(word>>8);
    bytes[3]=uint8_t(word);
}

void sealChunk(uint8_t *chunk, uint32_t type, size_t payload) {
    writeWord(chunk,uint32_t(payload));
    writeWord(chunk+4,type);
    writeWord(chunk+8+payload,Crc32::compute(chunk+4,4+payload));
}

void header(uint8_t *bytes) {
    std::memcpy(bytes,Signature,SignatureBytes);
    auto *chunk=bytes+SignatureBytes;
    writeWord(chunk+8,BitmapLayout::Width);
    writeWord(chunk+12,BitmapLayout::Height);
    std::memcpy(chunk+16,Rgb8,sizeof Rgb8);
    sealChunk(chunk,IHDR,13);
}

bool readHeader(FIL &file) {
    uint8_t chunk[HeaderBytes-SignatureBytes];
    UINT read=0;
    return f_lseek(&file,SignatureBytes)==FR_OK &&
        f_read(&file,chunk,sizeof chunk,&read)==FR_OK && read==sizeof chunk &&
        readWord(chunk)==13 && readWord(chunk+4)==IHDR &&
        readWord(chunk+8)==BitmapLayout::Width && readWord(chunk+12)==BitmapLayout::Height &&
        std::memcmp(chunk+16,Rgb8,sizeof Rgb8)==0 &&
        readWord(chunk+21)==Crc32::compute(chunk+4,17);
}
}

namespace {
constexpr int WindowBits=11; // 2 KiB, enough to match one RGB scanline back.
}

bool PngStream::begin(scope::AcquisitionArena &arena, bool encode) {
    if (!memory_.claim(arena)) return false;
    encode_=encode;
    io_={};
    memory_.attach(io_);
    scanline_=allocate(png::ScanlineBytes);
    const int result=encode ? deflateInit2(&io_,Z_BEST_SPEED,Z_DEFLATED,WindowBits,1,Z_DEFAULT_STRATEGY)
                            : inflateInit2(&io_,WindowBits);
    if (result==Z_OK) return true;
    end();
    return false;
}

void PngStream::end() {
    if (!memory_.claimed()) return;
    if (encode_) deflateEnd(&io_);
    else inflateEnd(&io_);
    memory_.release();
}
}
