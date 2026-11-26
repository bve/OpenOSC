#include "openosc/ui/images/browser.hpp"
#include "openosc/storage/layout.hpp"
#include "openosc/ui/components/menu_row.hpp"
#include <cstdio>

namespace openosc::ui {
namespace {
lv_obj_t *text(lv_obj_t *parent,const char *value,int x,int y,int width) {
    auto *label=Elements::label(parent,value,x,y);
    lv_obj_set_size(label,width,18);
    lv_label_set_long_mode(label,LV_LABEL_LONG_MODE_DOTS);
    return label;
}
}
void ImageBrowserView::list(lv_obj_t *parent,const MenuPalette &palette,const ImageList &images) {
    char count[24];
    std::snprintf(count,sizeof count,"%u/%u",images.total ? images.selected+1 : 0,images.total);
    auto *counter=text(parent,count,230,7,62);
    lv_obj_set_style_text_align(counter,LV_TEXT_ALIGN_RIGHT,0);
    if (!images.total) {
        text(parent,"No screenshots",12,66,280);
        text(parent,storage::Layout::Screenshots,12,92,280);
        return;
    }
    for (unsigned i=0;i<images.visible && i<ImageList::Rows;++i)
        MenuRow(parent,32+i*26,24,images.names[i],"",images.first+i==images.selected,palette); // Five within the menu (ScreenLayout::MenuHeight).
}
void ImageBrowserView::preview(lv_obj_t *parent,const MenuPalette &palette,const char *name,const uint16_t *pixels, bool fullSize, const char *keys) {
    lv_obj_clean(parent);
    if (!fullSize) { // The name, then what the keys do (the cards are hidden under the picture).
        const int hint = keys && *keys ? Elements::textWidth(keys) + 4 : 0;
        auto *title=text(parent,name,8,3,304-8-hint-6);
        lv_obj_set_style_text_color(title,lv_color_hex(palette.accent),0);
        if (hint) {
            auto *line=text(parent,keys,312-hint-4,3,hint);
            lv_obj_set_style_text_color(line,lv_color_hex(palette.secondary),0);
            lv_obj_set_style_text_align(line,LV_TEXT_ALIGN_RIGHT,0);
        }
    }
    bitmap_={};
    bitmap_.header.magic=LV_IMAGE_HEADER_MAGIC;
    bitmap_.header.cf=LV_COLOR_FORMAT_RGB565;
    bitmap_.header.w=fullSize ? 320 : screenshot::BitmapLayout::PreviewWidth;
    bitmap_.header.h=fullSize ? 240 : screenshot::BitmapLayout::PreviewHeight;
    bitmap_.header.stride=bitmap_.header.w*2;
    bitmap_.data_size=bitmap_.header.w*bitmap_.header.h*2;
    bitmap_.data=reinterpret_cast<const uint8_t*>(pixels);
    auto *image=lv_image_create(parent);
    lv_image_set_src(image,&bitmap_);
    lv_obj_set_pos(image,fullSize ? 0 : 32,fullSize ? 0 : 26);
}
} // namespace openosc::ui
