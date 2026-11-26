#pragma once
#include "openosc/screenshot/bmp.hpp"
#include "openosc/screenshot/png.hpp"
#include "openosc/storage/filesystem.hpp"

namespace openosc {
class ScreenshotWriter {
public:
    explicit ScreenshotWriter(Filesystem& fs) : fs_(fs) {}
    bool start(const uint16_t* frame, scope::AcquisitionArena &arena);
    void service();
    // Abandons an unfinished screenshot: the file is closed and its temporary name removed, so the
    // final name never appears and service() writes nothing more. Nothing happens when none is open.
    void cancel();
    bool busy() const { return open_; }
    bool succeeded() const { return complete_; }
    unsigned percent() const { return complete_ ? 100 : image_.rows()*99/BmpImage::Height; }
    const char* error() const { return error_; }
    const char* path() const { return final_; }
    // Read the sequence prefix of a filename in the screenshot namespace, or return 0.
    static uint32_t number(const char* name);
private:
    bool fail(const char* message);
    bool highestNumber(uint32_t& highest); // Over the screenshot folder; false: it cannot be read.
    Filesystem& fs_;
    screenshot::PngEncoder image_;
    FIL file_{};
    bool open_ = false, complete_ = false;
    uint32_t bytesLeft_ = 0, nextId_ = 1;
    const char* error_ = nullptr;
    char temporary_[40]{}, final_[48]{}; // "/openosc/SCREENSHOTS/SCR00001-0123ABCD.PNG".
    alignas(4) uint8_t buffer_[4096];
};
}
