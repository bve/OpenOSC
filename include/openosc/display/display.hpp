#pragma once
#include <cstdint>
#include "openosc/display/boot_splash.hpp"
namespace openosc {
class Display {
public:
    static constexpr unsigned Width = 320, Height = 240;
    static constexpr uint32_t FrameAddress = 0x24000000;
    static constexpr uint16_t Background = 0x0841, White = 0xffff,
        Cyan = 0x07ff, Green = 0x07e0, Red = 0xf800, Selected = 0x21cd;
    bool initialize(bool bootLogo = false);
    static bool scanningBootBuffer(uint32_t frame = FrameAddress);
    // One-use handoff from the second stage to the application.
    void offerBootFrame() const;
    static bool takeBootFrame(boot_splash::Style *style = nullptr);
    void clear(uint16_t color = Background);
    void rectangle(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t color);
    void text(unsigned x, unsigned y, const char* text, uint16_t color = White);
    void number(unsigned x, unsigned y, uint32_t value, unsigned digits = 0,
                unsigned base = 10, uint16_t color = White);
    void progress(const char* stage, uint32_t done, uint32_t total);
    void present();
    // A stalled controller: reset (its memory reads stop) and set up again scanning `frame`.
    static bool restart(const void *frame);
private:
    static bool configure(uint32_t frame);
    boot_splash::Style bootStyle_ = boot_splash::Style::Count;
};
}

