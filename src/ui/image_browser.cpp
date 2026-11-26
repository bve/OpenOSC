#include "openosc/ui/application_view.hpp"

namespace openosc {
void ApplicationView::images(const ui::ImageList &images) {
    menuFrame("SCREENSHOTS");
    imageBrowser_.list(menu_,menuPalette_,images);
}
// The picture keeps its size (256x192 fitted, or 320x240 at 1:1 over everything): no room for the
// cards, so they hide and the fitted view's header says the keys in a line (`keys`, SoftKeys::line).
void ApplicationView::imagePreview(const char *name,const uint16_t *pixels, bool fullSize, const char *keys) {
    dismissNotice(notice_);
    menuFrame("IMAGE");
    host_.overlay(false);
    lv_obj_set_pos(menu_,0,fullSize ? 0 : 20);
    lv_obj_set_size(menu_,320,fullSize ? 240 : 220);
    lv_obj_set_style_border_width(menu_,fullSize ? 0 : 1,0);
    imageBrowser_.preview(menu_,menuPalette_,name,pixels,fullSize,keys);
}
void ApplicationView::sessions(const ui::ImageList &list) {
    menuFrame("DMM SESSIONS");
    imageBrowser_.list(menu_,menuPalette_,list);
}
void ApplicationView::sessionTrend(const char *name, ui::SessionTrendView &view, const ui::SessionTrendData &data, bool rebuild) {
    if (rebuild || !sessionTrendLive_ || sessionTrend_ != &view || lv_obj_is_hidden(menu_)) {
        menuFrame(name);
        view.create(menu_, menuPalette_);
        sessionTrend_ = &view;
        sessionTrendLive_ = true;
    }
    view.show(data);
    if (toastShowing()) lv_obj_move_foreground(toast_);
}
void ApplicationView::sessionsClosed() {
    if (sessionTrendLive_ && sessionTrend_) sessionTrend_->destroy();
    sessionTrendLive_ = false;
    sessionTrend_ = nullptr;
}
} // namespace openosc
