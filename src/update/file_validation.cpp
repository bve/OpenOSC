#include "openosc/update/update_operation.hpp"
#include "openosc/update/crc32.hpp"
#include "openosc/update/image_check.hpp"
#include "openosc/platform/board.hpp"
#include <cstring>

namespace openosc {
bool UpdateOperation::fail(const char* message) { error_ = message; return false; }
bool UpdateOperation::validateFile(FIL& file) {
    const char* message = ImageCheck::file(file, fileKind_, image_, payloadOffset_, buffer_, sizeof buffer_,
        [](void* screen, uint32_t done, uint32_t total) { static_cast<Display*>(screen)->progress("CHECKING FILE", done, total); },
        &screen_);
    if (message) return fail(message);
    error_ = nullptr;
    return true;
}
bool UpdateOperation::inspect(const char* path) {
    inspected_ = false;
    fileKind_ = ImageFile::kind(path);
    if (fileKind_ == ImageFile::Kind::Unsupported) return fail("UNSUPPORTED IMAGE FILE EXTENSION");
    FIL file{};
    if (f_open(&file, path, FA_READ) != FR_OK) return fail("CANNOT OPEN IMAGE FILE");
    bool result = validateFile(file);
    if (f_close(&file) != FR_OK) return fail("CANNOT CLOSE IMAGE FILE");
    inspected_ = result;
    return result;
}
bool UpdateOperation::installed(bool progress) {
    inspected_ = false;
    payloadOffset_ = sizeof image_;
    std::memcpy(&image_, reinterpret_cast<const void*>(layout::ApplicationBase), sizeof image_);
    ImageError result = ImageValidator::header(image_, sizeof image_ + image_.payloadBytes);
    if (result != ImageError::None) return fail(ImageValidator::message(result));
    const auto* words = reinterpret_cast<const uint32_t*>(layout::VectorBase);
    result = ImageValidator::vectors(image_, words[0], words[1]);
    if (result != ImageError::None) return fail(ImageValidator::message(result));
    Crc32 crc;
    for (uint32_t offset = 0; offset < image_.payloadBytes; ) {
        uint32_t chunk = image_.payloadBytes - offset;
        if (chunk > 4096) chunk = 4096;
        crc.update(reinterpret_cast<const void*>(layout::VectorBase + offset), chunk);
        offset += chunk;
        if (progress) screen_.progress("CHECKING INSTALLED IMAGE", offset, image_.payloadBytes);
        Board::service();
    }
    if (crc.value() != image_.payloadCrc) return fail("INSTALLED IMAGE CRC FAILED");
    error_ = nullptr;
    return true;
}
}
