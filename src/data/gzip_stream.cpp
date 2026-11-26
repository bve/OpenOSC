#include "openosc/data/gzip_stream.hpp"
#include <cstring>
#include <new>
#include <zlib.h>

namespace openosc {
bool GzipStream::begin(scope::AcquisitionArena &arena) {
    if (active() || !memory_.claim(arena)) return false;
    void *stream = memory_.allocate(sizeof(z_stream));
    if (!stream) { memory_.release(); return false; }
    io_ = new (stream) z_stream{};
    memory_.attach(*io_);
    crc_ = Crc32{};
    total_ = trailer_ = 0;
    ended_ = false;
    // Negative window bits: raw DEFLATE, the gzip wrapper is this class's.
    if (deflateInit2(io_, Level, Z_DEFLATED, -WindowBits, MemoryLevel, Z_DEFAULT_STRATEGY) != Z_OK) {
        memory_.release();
        io_ = nullptr;
        return false;
    }
    input_ = memory_.allocate(InputBytes);
    if (input_) return true;
    end();
    return false;
}
void GzipStream::end() {
    if (!active()) return;
    deflateEnd(io_);
    memory_.release();
    io_ = nullptr;
    input_ = nullptr;
}
void GzipStream::header(uint8_t *out) {
    static constexpr uint8_t Header[HeaderBytes] = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 255};
    std::memcpy(out, Header, HeaderBytes);
}
bool GzipStream::compress(const uint8_t *in, size_t bytes, bool finish, uint8_t *out, size_t capacity, size_t &consumed, size_t &produced) {
    consumed = produced = 0;
    if (!active() || (bytes && !in) || (capacity && !out)) return false;
    if (!ended_) {
        io_->next_in = const_cast<Bytef *>(in); io_->avail_in = uInt(bytes);
        io_->next_out = out; io_->avail_out = uInt(capacity);
        const int result = deflate(io_, finish ? Z_FINISH : Z_NO_FLUSH);
        consumed = bytes - io_->avail_in;
        produced = capacity - io_->avail_out;
        crc_.update(in, consumed);
        total_ += consumed;
        if (result == Z_STREAM_END) ended_ = true;
        else if (result != Z_OK && result != Z_BUF_ERROR) return false; // Z_BUF_ERROR: no room, try again.
    }
    if (ended_) { // The trailer, little-endian, as far as there is room.
        const uint32_t words[2] = {crc_.value(), uint32_t(total_)};
        while (trailer_ < TrailerBytes && produced < capacity)
            out[produced++] = uint8_t(words[trailer_ / 4] >> (8 * (trailer_ % 4))), ++trailer_;
    }
    return true;
}
} // namespace openosc
