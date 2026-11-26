#include "openosc/screenshot/png.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace openosc::screenshot {
namespace {
constexpr unsigned InputBytes=512;

bool readExact(FIL &file, void *data, UINT size) {
    UINT read=0;
    return f_read(&file,data,size,&read)==FR_OK && read==size;
}

uint8_t paeth(uint8_t left, uint8_t above, uint8_t corner) {
    const int fromLeft=int(above)-corner, fromAbove=int(left)-corner;
    const int dl=std::abs(fromLeft), da=std::abs(fromAbove), dc=std::abs(fromLeft+fromAbove);
    return dl<=da && dl<=dc ? left : da<=dc ? above : corner;
}
}

bool PngReader::start(FIL &file, scope::AcquisitionArena &arena) {
    if (!png::readHeader(file) || !stream_.begin(arena,false)) return false;
    input_=stream_.allocate(InputBytes);
    if (!input_) { cancel(); return false; }
    remaining_=0;
    row_=previewRow_=0;
    seenData_=drain_=ended_=complete_=false;
    auto &io=stream_.io();
    io.next_out=stream_.scanline();
    io.avail_out=png::ScanlineBytes;
    return true;
}

bool PngReader::refill(FIL &file) {
    using namespace png;
    auto &io=stream_.io();
    if (!remaining_) {
        uint8_t chunk[8];
        if (!readExact(file,chunk,sizeof chunk)) return false;
        const uint32_t size=readWord(chunk), type=readWord(chunk+4);
        const auto left=f_size(&file)-f_tell(&file);
        if (left<4 || size>left-4) return false;
        if (type==IEND) {
            uint8_t checksum[4];
            if (size || !ended_ || left!=4 || !readExact(file,checksum,sizeof checksum) ||
                readWord(checksum)!=Crc32::compute(chunk+4,4)) return false;
            stream_.end();
            complete_=true;
            return true;
        }
        if (type!=IDAT) {
            // Ancillary chunks may precede or follow the image stream.
            return (chunk[4]&0x20) && (!seenData_ || ended_) &&
                f_lseek(&file,f_tell(&file)+size+4)==FR_OK;
        }
        if (ended_ && size) return false;
        seenData_=true;
        remaining_=size;
        crc_=Crc32{};
        crc_.update(chunk+4,4);
    }
    const unsigned count=std::min<uint32_t>(remaining_,InputBytes);
    if (count && !readExact(file,input_,count)) return false;
    crc_.update(input_,count);
    remaining_-=count;
    if (!remaining_) {
        uint8_t checksum[4];
        if (!readExact(file,checksum,sizeof checksum) || readWord(checksum)!=crc_.value()) return false;
    }
    io.next_in=input_;
    io.avail_in=count;
    return true;
}

bool PngReader::commitRow(uint8_t *previous, uint16_t *preview, bool fullSize) {
    if (row_==BitmapLayout::Height) return false;
    uint8_t *line=stream_.scanline();
    const unsigned filter=*line++;
    if (filter>4) return false;
    if (filter) {
        for (unsigned i=0; i<png::RowBytes; ++i) {
            const uint8_t left=i>=3 ? line[i-3] : 0;
            const uint8_t above=row_ ? previous[i] : 0;
            const uint8_t corner=row_ && i>=3 ? previous[i-3] : 0;
            unsigned prediction=0;
            switch (filter) {
            case 1: prediction=left; break;
            case 2: prediction=above; break;
            case 3: prediction=(unsigned(left)+above)/2; break;
            case 4: prediction=paeth(left,above,corner); break;
            }
            line[i]=uint8_t(line[i]+prediction);
        }
    }
    const unsigned height=fullSize ? BitmapLayout::Height : BitmapLayout::PreviewHeight;
    if (previewRow_*BitmapLayout::Height/height==row_) {
        const unsigned width=fullSize ? BitmapLayout::Width : BitmapLayout::PreviewWidth;
        auto *target=preview+previewRow_++*width;
        for (unsigned x=0; x<width; ++x) {
            const auto *pixel=line+(x*BitmapLayout::Width/width)*3;
            target[x]=uint16_t((pixel[0]>>3)<<11)|uint16_t((pixel[1]>>2)<<5)|(pixel[2]>>3);
        }
    }
    std::memcpy(previous,line,png::RowBytes);
    ++row_;
    return true;
}

bool PngReader::service(FIL &file, uint8_t *previous, uint16_t *preview, bool fullSize) {
    auto &io=stream_.io();
    for (unsigned step=0; step<8 && !complete_; ++step) {
        if (!ended_ && (io.avail_in || drain_)) {
            const int result=inflate(&io,Z_NO_FLUSH);
            if (result!=Z_OK && result!=Z_STREAM_END && result!=Z_BUF_ERROR) return false;
            drain_=!io.avail_out;
            if (drain_) {
                if (!commitRow(previous,preview,fullSize)) return false;
                io.next_out=stream_.scanline();
                io.avail_out=row_<BitmapLayout::Height ? png::ScanlineBytes : 1;
            }
            if (result==Z_STREAM_END) {
                if (row_!=BitmapLayout::Height || io.avail_in || remaining_) return false;
                ended_=true;
            }
        } else if (!refill(file)) return false;
    }
    return true;
}
}
