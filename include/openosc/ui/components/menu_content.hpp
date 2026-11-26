#pragma once
#include "openosc/ui/components/menu_row.hpp"
#include <cstring>

namespace openosc::ui {
class MenuContent {
public:
    void reset() { for (auto &row:rows_) row=MenuRow{}; for (auto &label:labels_) label=nullptr; }
    void begin() {
        nextRow_=nextLabel_=0;
        for (auto &row:rows_) row.visible(false);
        for (auto *label:labels_) if (label) lv_obj_set_hidden(label,true);
    }
    // Texts of one draw: title, uptime, path or notice, header, page counter, "More". Past
    // Labels the last one is reused rather than writing beyond the array.
    static constexpr unsigned Labels=6;
    lv_obj_t *text(lv_obj_t *parent,const char *value,int x,int y) {
        auto *&label=labels_[nextLabel_<Labels ? nextLabel_++ : Labels-1];
        if (!label) label=Elements::label(parent,value,x,y);
        else if (std::strcmp(lv_label_get_text(label),value)) lv_label_set_text(label,value);
        lv_obj_set_pos(label,x,y); lv_obj_set_hidden(label,false);
        lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_LEFT,0);
        return label;
    }
    MenuRow &nextRow() { return rows_[nextRow_++]; }
    MenuRow &row(lv_obj_t *parent,int y,int height,const char *name,const char *value,
                 bool selected,const MenuPalette &palette) {
        auto &row=nextRow();
        row.update(parent,y,height,name,value,selected,palette);
        return row;
    }
private:
    MenuRow rows_[5];
    lv_obj_t *labels_[Labels]{};
    unsigned nextRow_=0,nextLabel_=0;
};
}
