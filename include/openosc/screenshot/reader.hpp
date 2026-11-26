#pragma once
#include "openosc/screenshot/bitmap_layout.hpp"
#include "openosc/screenshot/png.hpp"
#include "openosc/storage/filesystem.hpp"

namespace openosc::screenshot {
class ImageReader {
public:
    explicit ImageReader(Filesystem &fs) : fs_(fs) {}
    ~ImageReader() { cancel(); }
    bool start(const char *path,uint16_t *preview, scope::AcquisitionArena &arena, bool fullSize = false);
    void service();
    void cancel();
    bool busy() const { return open_; }
    bool ready() const { return ready_; }
    unsigned percent() const { return isPng_ ? png_.rows()*100/BitmapLayout::Height : row_*100/height_; }
    const char *error() const { return error_; }
private:
    bool fail(const char *message);
    void finish();
    Filesystem &fs_;
    FIL file_{};
    BitmapLayout layout_;
    PngReader png_;
    uint16_t *preview_=nullptr;
    unsigned row_=0, width_=BitmapLayout::PreviewWidth, height_=BitmapLayout::PreviewHeight;
    bool open_=false,ready_=false,isPng_=false;
    const char *error_=nullptr;
    uint8_t rowData_[BitmapLayout::Width*3];
};
uint16_t *previewMemory();
} // namespace openosc::screenshot
