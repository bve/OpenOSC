#include "openosc/ui/lvgl_display.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/display/display.hpp"
#include "openosc/platform/power.hpp"
#include "openosc/platform/cooperative_service.hpp"
#include "openosc/platform/interrupts.hpp"
#include "openosc/ui/render_idle.hpp"
#include <cstring>

extern "C" void lv_draw_dma2d_interrupt_handler(void); // vendor/lvgl dma2d (LV_DRAW_DMA2D_ASYNC_NO_OS).

namespace openosc {
namespace {
alignas(64) uint16_t framebuffers[2][Display::Width * Display::Height] __attribute__((section(".framebuffer")));
void resetRenderer() {
    __HAL_RCC_DMA2D_CLK_ENABLE();
    __HAL_RCC_DMA2D_FORCE_RESET(); __HAL_RCC_DMA2D_RELEASE_RESET();
    HAL_NVIC_ClearPendingIRQ(DMA2D_IRQn);
}
// Share AXI with USB DMA, acquisition snapshots and LTDC during full redraws. Short gaps let panel
// redraws take the 3.3 V rail below USB33RDY and drop the USB bus (6 per 100 open/close cycles
// with 8 clocks, 0 per 340 with 200). LVGL zeroes AMTCR at its start, a reset as well.
void shareBus() { DMA2D->AMTCR = (200U << DMA2D_AMTCR_DT_Pos) | DMA2D_AMTCR_EN; }
// FrameFlip's view of the LTDC.
class Ltdc final : public FrameFlip::Port {
public:
    void show(const void *frame, bool immediate) override {
        LTDC_Layer1->CFBAR = uint32_t(reinterpret_cast<uintptr_t>(frame));
        LTDC->SRCR = immediate ? LTDC_SRCR_IMR : LTDC_SRCR_VBR;
    }
    bool reloading() const override { return LTDC->SRCR & (LTDC_SRCR_VBR | LTDC_SRCR_IMR); }
    bool scanning() const override { return LTDC->GCR & LTDC_GCR_LTDCEN; }
    bool restart(const void *frame) override { return Display::restart(frame); }
};
Ltdc ltdc;
} // namespace
// A transfer that ABORT did not stop (LVGL's DMA2D unit, LV_DRAW_DMA2D_RESET_HOOK): the reset
// stops the writer before its buffer and task are released; the bus sharing comes back.
extern "C" void openosc_dma2d_reset(void) {
    resetRenderer();
    shareBus();
}
bool LvglDisplay::initialize() {
    resetRenderer();
    std::memset(framebuffers[1], 0, sizeof framebuffers[1]);
    Display panel;
    if (!Display::scanningBootBuffer() && !panel.initialize(true)) return false;
    flip_.attach(ltdc, framebuffers[0]);
    lv_init();
    // Completion is the START bit, read in the main context; the IRQ clears flags, notes errors
    // and requests a dispatch. Below acquisition (5) and USB (4).
    Interrupts::attach(DMA2D_IRQn, lv_draw_dma2d_interrupt_handler, 8);
    shareBus(); // After lv_init: LVGL zeroes AMTCR.
    lv_tick_set_cb(Board::milliseconds);
    display_ = lv_display_create(Display::Width, Display::Height);
    if (!display_) return false;
    lv_display_set_user_data(display_, this);
    lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
    // Keep buffer 0 on the panel until LVGL presents its first complete screen.
    lv_display_set_buffers(display_, framebuffers[1], framebuffers[0], sizeof framebuffers[0], LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display_, flush);
    lv_display_set_flush_wait_cb(display_, wait);
    front_ = framebuffers[0];
    return true;
}
// The flip is done: the frame is on screen and LVGL may draw into the other buffer.
bool LvglDisplay::complete(uint32_t now) {
    if (!flip_.pending()) return true;
    const void *shown = flip_.done(now);
    if (!shown) return false;
    adopt(shown);
    return true;
}
// The flip is done: `shown` is the front; LVGL gets the other buffer.
void LvglDisplay::adopt(const void *shown) {
    front_ = static_cast<const uint16_t*>(shown);
    ++flushes_;
    lv_display_flush_ready(display_);
}
void LvglDisplay::render() {
    if (waking_) resume();
    if (complete(Board::milliseconds())) lv_timer_handler(); // Not while the panel still shows the back buffer.
}
// The LTDC restarted on the latest presented frame, then the backlight; a failed restart leaves
// it off and dark (render() retries).
void LvglDisplay::resume() {
    const void *shown = nullptr;
    waking_ = !flip_.started(shown);
    if (waking_) { LTDC->GCR &= ~LTDC_GCR_LTDCEN; flip_.stopped(Board::milliseconds()); return; }
    if (shown) adopt(shown);
    PowerControl::backlight(true);
}
void LvglDisplay::settle() {
    while (!complete(Board::milliseconds())) {
        Board::service();
        CooperativeService::poll(Board::milliseconds());
        RenderIdle::run(Board::milliseconds());
    }
}
void LvglDisplay::enabled(bool enabled) {
    if (enabled && display_) {
        if (flip_.stopped()) resume();
        else PowerControl::backlight(true);
    } else {
        waking_ = false;
        PowerControl::backlight(false); LTDC->GCR &= ~LTDC_GCR_LTDCEN; flip_.stopped(Board::milliseconds());
    }
}

void LvglDisplay::flush(lv_display_t* display, const lv_area_t*, uint8_t* pixels) {
    auto& self = *static_cast<LvglDisplay*>(lv_display_get_user_data(display));
    if (!lv_display_flush_is_last(display)) { lv_display_flush_ready(display); return; } // Direct mode: drawn in place.
    __DSB();
    self.flip_.present(pixels, Board::milliseconds()); // flush_ready when the flip is done (complete).
}
// LVGL must have the flip done before it draws into the other buffer (lv_refr_now).
void LvglDisplay::wait(lv_display_t* display) {
    static_cast<LvglDisplay*>(lv_display_get_user_data(display))->settle();
}
}
