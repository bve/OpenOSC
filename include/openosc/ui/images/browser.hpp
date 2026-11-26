#pragma once
#include "openosc/screenshot/bitmap_layout.hpp"
#include "openosc/ui/components/elements.hpp"
#include "openosc/ui/components/menu_palette.hpp"

namespace openosc::ui {
// A page of a file list (the screenshots, the DMM sessions): its names, an optional value a row, and
// what an empty list says.
struct ImageList {
    static constexpr unsigned Rows=5;
    const char *names[Rows]{};
    const char *values[Rows]{};
    unsigned visible=0,selected=0,first=0,total=0;
    const char *empty="No screenshots", *location=nullptr; // location: none, the screenshots' directory.
};
class ImageBrowserView {
public:
    void list(lv_obj_t *parent,const MenuPalette &palette,const ImageList &images);
    // keys: the live keys in one line (SoftKeys::line), in the fitted view's header.
    void preview(lv_obj_t *parent,const MenuPalette &palette,const char *name,const uint16_t *pixels, bool fullSize = false, const char *keys = "");
private:
    lv_image_dsc_t bitmap_{};
};
} // namespace openosc::ui
