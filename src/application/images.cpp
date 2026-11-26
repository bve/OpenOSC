#include "openosc/application/controller.hpp"
#include "openosc/platform/input.hpp"
#include <cstdio>

namespace openosc {
void ApplicationController::showImages() {
    ui::ImageList list;
    static_assert(ui::ImageList::Rows==screenshot::ImageCatalog::PageSize);
    list.visible=images_.visible(); list.selected=images_.selected();
    list.first=images_.first(); list.total=images_.count();
    for (unsigned i=0;i<list.visible;++i) list.names[i]=images_.entry(i).name;
    view_.images(list);
}
void ApplicationController::openImages(uint32_t now) {
    if (shots_.busy()) { view_.notify(Tooltip::Unavailable, "Finish screenshot first",now); return; }
    if (!cancelAuto(now)) return;
    state_.settingsOpen=false; panel_=Panel::Images; imageShowing_=false; imageFullSize_=false;
    if (!images_.refresh()) view_.notify(Tooltip::Critical, images_.error(),now);
    showImages();
}
void ApplicationController::closeImages() {
    imageReader_.cancel(); images_.cancelClear(); imageShowing_=false;
    panel_=Panel::None; state_.settingsOpen=true;
    navigator_.reset(settingsMenu_.selected());
    showSettings();
}
void ApplicationController::loadImage(uint32_t now) {
    char path[screenshot::ImageCatalog::PathBytes];
    imageReader_.cancel(); imageShowing_=false;
    if (!images_.path(path,sizeof path)) return;
    if (screenLease_.busy(now)) { showImages(); view_.notify(Tooltip::Unavailable, "Image: USB screen transfer in progress",now); return; }
    screenLease_.revoke();
    if (!imageReader_.start(path,screenshot::previewMemory(),data_.scopeArena(),imageFullSize_)) {
        showImages(); view_.notify(Tooltip::Critical, imageReader_.error(),now); return;
    }
    imageProgress_=0;
    view_.notify(Tooltip::Progress, "Loading image...",now);
}
void ApplicationController::imageKeys(uint32_t keys,uint32_t now) {
    if (!keys) return;
    if (keys&Keypad::Mode) { closeImages(); closeSettings(); return; }
    if (keys&Keypad::F4) {
        if (imageShowing_ || imageReader_.busy()) { imageReader_.cancel(); imageShowing_=false; showImages(); }
        else closeImages();
        return;
    }
    if (images_.clearing()) return;
    if ((keys&Keypad::F2) && (imageShowing_ || imageReader_.busy())) {
        imageFullSize_=!imageFullSize_; loadImage(now); return;
    }
    const bool viewing=imageShowing_ || imageReader_.busy();
    if (keys&Keypad::F3) {
        imageReader_.cancel();
        if (images_.count()) confirmOperation(ui::Confirmation("DELETE THIS IMAGE?","Delete selected image"),&ApplicationController::deleteImage);
        return;
    }
    const bool previous=keys&(Keypad::Up|Keypad::Left),next=keys&(Keypad::Down|Keypad::Right);
    if (previous || next) {
        imageReader_.cancel();
        if (!images_.move(next ? 1 : -1) && images_.error()) view_.notify(Tooltip::Critical, images_.error(),now);
        if (viewing) loadImage(now);
        else showImages();
    }
    if (keys&Keypad::Menu) {
        if (viewing) { imageReader_.cancel(); imageShowing_=false; showImages(); }
        else loadImage(now);
    }
}
void ApplicationController::deleteImage(uint32_t now) {
    confirmationHandler_=nullptr; panel_=Panel::Images; imageShowing_=false;
    if (!images_.eraseSelected()) view_.notify(images_.error() ? Tooltip::Critical : Tooltip::Unavailable, images_.error() ? images_.error() : "No image selected",now);
    showImages();
}
void ApplicationController::clearImages(uint32_t now) {
    confirmationHandler_=nullptr; panel_=Panel::Images; imageShowing_=false;
    imageReader_.cancel();
    if (!images_.beginClear()) view_.notify(Tooltip::Critical, images_.error(),now);
    showImages(); imageProgress_=0;
}
void ApplicationController::serviceImages(uint32_t now) {
    if (panel_!=Panel::Images) return;
    if (images_.clearing()) {
        const unsigned total=images_.count();
        if (!images_.clearStep()) {
            const char *error=images_.error(); images_.refresh(); showImages(); view_.notify(Tooltip::Critical, error,now);
        }
        else if (!images_.clearing() || images_.erased()!=imageProgress_) {
            const bool complete=!images_.clearing();
            if (complete) showImages();
            imageProgress_=images_.erased();
            char progress[48];
            std::snprintf(progress,sizeof progress,"%s: %u/%u",complete ? "Screenshots cleared" : "Deleting images",
                          imageProgress_,total);
            view_.notify(complete ? Tooltip::Done : Tooltip::Progress, progress,now);
        }
    }
    if (!imageReader_.busy()) return;
    imageReader_.service();
    if (imageReader_.busy()) return;
    if (imageReader_.ready()) {
        imageShowing_=true;
        char keys[64]; imageSoftKeys().line(keys,sizeof keys);
        view_.imagePreview(images_.name(),screenshot::previewMemory(),imageFullSize_,keys);
    } else { showImages(); view_.notify(Tooltip::Critical, imageReader_.error(),now); }
}
} // namespace openosc
