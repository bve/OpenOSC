#pragma once
#include "openosc/update/image_file.hpp"
#include "ff.h"
#include <cstddef>

namespace openosc {
// The whole check of an image file, shared by Recovery (before it flashes) and by the SCPI upload (before
// it replaces a file): extension, header, trailer of a BIN, vectors and the CRC of the whole payload.
class ImageCheck {
public:
    using Progress = void (*)(void* context, uint32_t done, uint32_t total);
    // `file` is open for reading at offset 0. `buffer` (any size >= 8 bytes) is the read scratch; `progress` may be null.
    // On success returns nullptr, `image` is the header (a BIN's is built from its trailer and payload CRC)
    // and `payloadOffset` is 0 for a raw BIN. On failure returns the message and the outputs are unspecified.
    static const char* file(FIL& file, ImageFile::Kind kind, ImageHeader& image, uint32_t& payloadOffset,
                            uint8_t* buffer, size_t bufferBytes, Progress progress, void* context);
};
}
