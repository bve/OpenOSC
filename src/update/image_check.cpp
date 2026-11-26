#include "openosc/update/image_check.hpp"
#include "openosc/update/crc32.hpp"
#include <cstring>

namespace openosc {
const char* ImageCheck::file(FIL& file, ImageFile::Kind kind, ImageHeader& image, uint32_t& payloadOffset,
                             uint8_t* buffer, size_t bufferBytes, Progress progress, void* context) {
    UINT count = 0;
    const uint32_t bytes = f_size(&file);
    if (bytes > sizeof image + layout::MaxPayload) return "IMAGE FILE IS TOO LARGE";
    image = {};
    if (f_read(&file, &image, sizeof image, &count) != FR_OK)
        return "CANNOT READ IMAGE FILE";
    ImageError result = ImageFile::decode(image, count, bytes, kind, payloadOffset);
    if (result != ImageError::None) return ImageValidator::message(result);
    const bool raw = payloadOffset == 0;
    ImageTrailer trailer{};
    if (raw) {
        if (f_lseek(&file, bytes - sizeof trailer) != FR_OK
            || f_read(&file, &trailer, sizeof trailer, &count) != FR_OK || count != sizeof trailer)
            return "CANNOT READ BOARD MARKER";
        result = ImageValidator::trailer(trailer, bytes);
        if (result != ImageError::None) return ImageValidator::message(result);
    }
    if (f_lseek(&file, payloadOffset) != FR_OK) return "CANNOT SEEK IMAGE PAYLOAD";
    Crc32 crc;
    uint32_t position = 0;
    while (position < image.payloadBytes) {
        uint32_t chunk = image.payloadBytes - position;
        if (chunk > bufferBytes) chunk = uint32_t(bufferBytes);
        // A raw BIN carries its program CRC, so stop exactly where the program ends.
        if (raw && position < trailer.programBytes && chunk > trailer.programBytes - position)
            chunk = trailer.programBytes - position;
        if (f_read(&file, buffer, chunk, &count) != FR_OK || count != chunk)
            return "IMAGE FILE IS INCOMPLETE";
        if (!position) {
            uint32_t words[2];
            std::memcpy(words, buffer, sizeof words);
            result = ImageValidator::vectors(image, words[0], words[1]);
            if (result != ImageError::None) return ImageValidator::message(result);
        }
        crc.update(buffer, count);
        position += count;
        if (raw && position == trailer.programBytes && crc.value() != trailer.programCrc)
            return "FULL FILE CRC FAILED";
        if (progress) progress(context, position, image.payloadBytes);
    }
    if (raw) {
        image.payloadCrc = crc.value();
        image.headerCrc = ImageValidator::headerCrc(image);
    } else if (crc.value() != image.payloadCrc) return "FULL FILE CRC FAILED";
    return nullptr;
}
}
