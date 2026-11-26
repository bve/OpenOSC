#include "openosc/screenshot/png.hpp"

namespace openosc::screenshot {
namespace {
void unpack(uint16_t pixel, uint8_t (&rgb)[3]) {
    const unsigned red=pixel>>11, green=(pixel>>5)&63U, blue=pixel&31U;
    rgb[0]=uint8_t((red<<3)|(red>>2));
    rgb[1]=uint8_t((green<<2)|(green>>4));
    rgb[2]=uint8_t((blue<<3)|(blue>>2));
}

unsigned magnitude(uint8_t difference) { return difference<128 ? difference : 256-difference; }
}

bool PngEncoder::start(scope::AcquisitionArena &arena, const uint16_t *pixels) {
    if (!stream_.begin(arena,true)) return false;
    pixels_=pixels;
    row_=0;
    first_=true;
    chunkActive_=ended_=complete_=false;
    return true;
}

void PngEncoder::prepareRow() {
    const auto *pixels=pixels_+row_*BitmapLayout::Width;
    const auto *abovePixels=row_ ? pixels-BitmapLayout::Width : nullptr;
    auto *line=stream_.scanline();
    // Choose Sub or Up by the sum of signed residual magnitudes. Compute and
    // keep one candidate in place, then replace it only if Sub wins.
    unsigned subCost=0, upCost=0;
    uint8_t left[3]{};
    for (unsigned x=0; x<BitmapLayout::Width; ++x) {
        uint8_t rgb[3], above[3]{};
        unpack(pixels[x],rgb);
        if (abovePixels) unpack(abovePixels[x],above);
        for (unsigned component=0; component<3; ++component) {
            const uint8_t up=uint8_t(rgb[component]-above[component]);
            line[1+3*x+component]=up;
            upCost+=magnitude(up);
            subCost+=magnitude(uint8_t(rgb[component]-left[component]));
            left[component]=rgb[component];
        }
    }
    line[0]=2;
    if (subCost<upCost) {
        line[0]=1;
        for (unsigned component=0; component<3; ++component) left[component]=0;
        for (unsigned x=0; x<BitmapLayout::Width; ++x) {
            uint8_t rgb[3];
            unpack(pixels[x],rgb);
            for (unsigned component=0; component<3; ++component) {
                line[1+3*x+component]=uint8_t(rgb[component]-left[component]);
                left[component]=rgb[component];
            }
        }
    }
}

bool PngEncoder::service(uint8_t *out, size_t capacity, size_t &bytes) {
    using namespace png;
    bytes=0;
    if (complete_) return true;
    if (capacity<=HeaderBytes+ChunkOverhead) return false;
    if (ended_) {
        sealChunk(out,IEND,0);
        bytes=ChunkOverhead;
        complete_=true;
        return true;
    }
    auto &io=stream_.io();
    if (!chunkActive_) {
        prefix_=first_ ? HeaderBytes : 0;
        if (first_) header(out);
        first_=false;
        io.next_out=out+prefix_+8;
        io.avail_out=uInt(capacity-prefix_-ChunkOverhead);
        chunkActive_=true;
    }
    for (unsigned step=0; step<8 && io.avail_out && !ended_; ++step) {
        if (!io.avail_in && row_<BitmapLayout::Height) {
            prepareRow();
            ++row_;
            io.next_in=stream_.scanline();
            io.avail_in=ScanlineBytes;
        }
        const bool finish=row_==BitmapLayout::Height;
        const int result=deflate(&io,finish ? Z_FINISH : Z_NO_FLUSH);
        if (result!=Z_OK && result!=Z_STREAM_END) return false;
        ended_=result==Z_STREAM_END;
    }
    if (!ended_ && io.avail_out) return true;
    const size_t payload=capacity-prefix_-ChunkOverhead-io.avail_out;
    if (payload) sealChunk(out+prefix_,IDAT,payload);
    bytes=prefix_+(payload ? ChunkOverhead+payload : 0);
    chunkActive_=false;
    if (ended_) {
        stream_.end();
        if (capacity-bytes>=ChunkOverhead) {
            sealChunk(out+bytes,IEND,0);
            bytes+=ChunkOverhead;
            complete_=true;
        }
    }
    return true;
}
}
