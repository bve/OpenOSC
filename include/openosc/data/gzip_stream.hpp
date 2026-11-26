#pragma once
#include "openosc/data/zlib_arena.hpp"
#include "openosc/update/crc32.hpp"

namespace openosc {
// One gzip member (RFC 1952) in slices, for waveform exports (GitHub #166). The bundled zlib is
// built with NO_GZIP and without its crc32, so this writes the 10-byte header and the trailer (the
// input's CRC-32, Crc32, and its size mod 2^32) around zlib's raw DEFLATE. Everything lives in the
// Trial area (ZlibArena): the z_stream, the deflate state (window 2^WindowBits, MemoryLevel) and
// InputBytes of input.
// begin() claims it, end() (also from the destructor) releases it, at once after complete() too.
class GzipStream {
public:
    static constexpr int Level = 6, WindowBits = 10, MemoryLevel = 1; // About 11 KiB of deflate state.
    static constexpr size_t HeaderBytes = 10, TrailerBytes = 8, InputBytes = 4096;
    GzipStream() = default;
    GzipStream(const GzipStream &) = delete;
    GzipStream &operator=(const GzipStream &) = delete;
    ~GzipStream() { end(); }
    // False (nothing claimed) when the Trial area is taken or the state does not fit.
    bool begin(scope::AcquisitionArena &arena);
    void end();
    bool active() const { return memory_.claimed(); }
    uint8_t *input() const { return input_; } // InputBytes, for the caller's next input.
    static void header(uint8_t *out); // HeaderBytes: deflate, no name, no time, unknown OS.
    // Compresses in[0, bytes): `consumed` of it taken, `produced` bytes into out[0, capacity). finish:
    // no input after this; call again (with what was not consumed) until complete(), the trailer
    // included. False on a zlib error.
    bool compress(const uint8_t *in, size_t bytes, bool finish, uint8_t *out, size_t capacity, size_t &consumed, size_t &produced);
    bool complete() const { return trailer_ == TrailerBytes; }
    size_t memoryBytes() const { return memory_.used(); } // Of the Trial area, while active (tests, map review).

private:
    z_stream_s *io_ = nullptr; // In the Trial area.
    ZlibArena memory_;
    Crc32 crc_;
    uint8_t *input_ = nullptr;
    uint64_t total_ = 0;
    size_t trailer_ = 0; // Trailer bytes written; deflate's end reached when ended_.
    bool ended_ = false;
};
} // namespace openosc
