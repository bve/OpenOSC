#include "openosc/screenshot/reader.hpp"
#include "openosc/acquisition/arena.hpp"

namespace openosc::screenshot {
void ImageReader::cancel() {
    if (open_) f_close(&file_);
    png_.cancel();
    open_=ready_=false;
    preview_=nullptr;
}
bool ImageReader::fail(const char *message) {
    cancel(); error_=message; return false;
}
bool ImageReader::start(const char *path,uint16_t *preview, scope::AcquisitionArena &arena, bool fullSize) {
    cancel(); error_=nullptr; row_=0;
    if (!path || !preview || !fs_.mounted()) return fail("Storage not available");
    if (f_open(&file_,path,FA_READ)!=FR_OK) return fail("Cannot open image");
    open_=true;
    uint8_t header[66]; UINT read=0;
    if (f_read(&file_,header,sizeof header,&read)!=FR_OK) return fail("Image read failed");
    isPng_=png::signature(header,read);
    if (isPng_) {
        if (arena.busy()&scope::AcquisitionArena::Trial) return fail("Screenshot workspace busy");
        if (!png_.start(file_,arena)) return fail("Unsupported or damaged PNG");
    } else if (!BitmapLayout::parse(header,read,f_size(&file_),layout_))
        return fail("Unsupported or damaged BMP");
    preview_=preview;
    width_=fullSize ? BitmapLayout::Width : BitmapLayout::PreviewWidth;
    height_=fullSize ? BitmapLayout::Height : BitmapLayout::PreviewHeight;
    return true;
}
void ImageReader::service() {
    if (!open_) return;
    if (isPng_) {
        if (!png_.service(file_,rowData_,preview_,width_==BitmapLayout::Width)) fail("Invalid or damaged PNG");
        else if (png_.complete()) finish();
        return;
    }
    for (unsigned n=0;n<4 && row_<height_;++n,++row_) {
        UINT count=0;
        const unsigned sourceRow=row_*BitmapLayout::Height/height_;
        if (f_lseek(&file_,layout_.rowOffset(sourceRow))!=FR_OK ||
            f_read(&file_,rowData_,layout_.stride,&count)!=FR_OK || count!=layout_.stride) {
            fail("Image read failed"); return;
        }
        layout_.previewRow(rowData_,preview_+row_*width_,width_==BitmapLayout::Width);
    }
    if (row_==height_) finish();
}
void ImageReader::finish() {
    const auto closed=f_close(&file_); open_=false;
    if (closed!=FR_OK) { fail("Cannot close image"); return; }
    ready_=true;
}
} // namespace openosc::screenshot
