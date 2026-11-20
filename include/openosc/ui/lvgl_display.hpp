#pragma once
#include "lvgl.h"
#include "openosc/display/boot_splash.hpp"
#include "openosc/ui/frame_flip.hpp"
#include <cstdint>

namespace openosc {
// LVGL on the panel: two framebuffers in direct mode, DMA2D transfers asynchronous (lv_conf.h).
// A finished frame is handed to the LTDC and render() returns at once; while that flip waits
// for the vertical blanking, render() does nothing, so the main loop goes on (acquisition, input,
// the next projection) instead of spinning. settle() waits for it where a caller needs the frame
// on screen (lv_refr_now: screenshots).
class LvglDisplay {
public:
    bool showBootSplash(bool inherited, boot_splash::Style style);
    bool initialize();
    void render();
    void enabled(bool enabled);
    void settle();
    const uint16_t* frame() const { return front_; }
    uint32_t flushes() const { return flushes_; }
private:
    static void flush(lv_display_t*, const lv_area_t*, uint8_t* pixels);
    static void wait(lv_display_t*);
    bool complete(uint32_t now);
    void adopt(const void *shown);
    void resume();
    lv_display_t* display_ = nullptr;
    const uint16_t* front_ = nullptr;
    uint32_t flushes_ = 0;
    uint32_t bootShownAt_ = 0;
    bool bootShowing_ = false;
    bool waking_ = false; // Switched on, the LTDC restart has not succeeded yet.
    FrameFlip flip_; // On the LTDC (lvgl_display.cpp).
};
}
