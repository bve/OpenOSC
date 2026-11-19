#include "openosc/application/controller.hpp"
#include <cstring>

namespace openosc {
void ApplicationController::startScreenshot(uint32_t now) {
    if (screenStreamBusy(now)) { view_.notify("Screenshot: USB screen transfer in progress",now); return; }
    lv_refr_now(nullptr);
    display_.settle(); // The frame just drawn is on screen: front() is it.
    auto *snapshot=screenshot::previewMemory();
    std::memcpy(snapshot,display_.frame(),BmpImage::PixelBytes);
    if (!shots_.start(snapshot)) {
        if (imageShowing_) loadImage(now);
        view_.notify(shots_.error(),now); return;
    }
    if (imageShowing_) showImages();
    shotProgress_=0;
    view_.notify("Saving screenshot: 0%",now);
}
}
