#include "openosc/application/controller.hpp"
#include <cstdio>
#include <cstring>

namespace openosc {
void ApplicationController::startScreenshot(uint32_t now) {
    if (screenLease_.busy(now)) { view_.notify("Screenshot: USB screen transfer in progress",now); return; }
    screenLease_.revoke(); // An idle stream's buffer is taken: its late reads are refused.
    view_.dismissNotice(ApplicationView::Notice::ScreenshotSaved);
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
void ApplicationController::finishScreenshot(uint32_t now) {
    if (imageShowing_) loadImage(now);
    if (!shots_.succeeded()) {
        view_.notify(shots_.error(), now);
        return;
    }
    const char *path = shots_.path();
    const char *separator = std::strrchr(path, '/');
    char message[40];
    std::snprintf(message, sizeof message, "Image saved as %s", separator ? separator + 1 : path);
    view_.notify(message, now, ApplicationView::Notice::ScreenshotSaved);
}
}
