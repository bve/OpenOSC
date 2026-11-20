#include "openosc/platform/watchdog.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/display/display.hpp"
#include "openosc/font.hpp"
namespace openosc {
namespace {
volatile uint16_t* pixels() { return reinterpret_cast<volatile uint16_t*>(Display::FrameAddress); }
}
void Display::clear(uint16_t color) {
    for (unsigned i = 0; i < Width * Height; ++i) {
        pixels()[i] = color;
        if (!(i & 1023)) Board::service();
    }
}
void Display::rectangle(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t color) {
    if (x >= Width || y >= Height) return;
    if (w > Width - x) w = Width - x;
    if (h > Height - y) h = Height - y;
    for (unsigned row = y; row < y + h; ++row)
        for (unsigned col = x; col < x + w; ++col) pixels()[row * Width + col] = color;
}
void Display::drawBootLogo() {
    // Five-column glyphs for the mixed-case OpenOSC wordmark. Bit 0 is the top pixel.
    constexpr uint8_t wordmark[][5] = {{0x3e, 0x41, 0x41, 0x41, 0x3e}, // O
                                      {0x7c, 0x14, 0x14, 0x14, 0x08}, // p
                                      {0x18, 0x2c, 0x2c, 0x2c, 0x10}, // e
                                      {0x3c, 0x04, 0x04, 0x04, 0x38}, // n
                                      {0x3e, 0x41, 0x41, 0x41, 0x3e}, // O
                                      {0x26, 0x49, 0x49, 0x49, 0x32}, // S
                                      {0x3e, 0x41, 0x41, 0x41, 0x22}}; // C
    constexpr unsigned scale = 5, glyphs = sizeof wordmark / sizeof wordmark[0];
    constexpr unsigned left = (Width - (glyphs * 6 - 1) * scale) / 2;
    constexpr unsigned top = (Height - 7 * scale) / 2;
    clear();
    for (unsigned glyph = 0; glyph < glyphs; ++glyph)
        for (unsigned col = 0; col < 5; ++col)
            for (unsigned row = 0; row < 7; ++row)
                if (wordmark[glyph][col] & (1U << row))
                    rectangle(left + (glyph * 6 + col) * scale, top + row * scale, scale, scale,
                              glyph < 4 ? White : Cyan);
}
void Display::text(unsigned x, unsigned y, const char* message, uint16_t color) {
    for (; *message && x < Width; ++message, x += 6) {
        char ch = *message;
        if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        unsigned glyph = 0;
        while (font::Alphabet[glyph] && font::Alphabet[glyph] != ch) ++glyph;
        if (!font::Alphabet[glyph]) continue;
        for (unsigned col = 0; col < 5; ++col)
            for (unsigned row = 0; row < 7; ++row)
                if ((font::Glyphs[glyph][col] & (1U << row)) && x + col < Width && y + row < Height)
                    pixels()[(y + row) * Width + x + col] = color;
    }
}
void Display::number(unsigned x, unsigned y, uint32_t value, unsigned digits, unsigned base, uint16_t color) {
    char buffer[11];
    if (base != 10 && base != 16) return;
    unsigned count = 0;
    do { unsigned digit = value % base; buffer[count++] = digit < 10 ? '0' + digit : 'A' + digit - 10; value /= base; }
    while (value && count < 10);
    while (count < digits && count < 10) buffer[count++] = '0';
    for (unsigned i = 0; i < count / 2; ++i) { char tmp = buffer[i]; buffer[i] = buffer[count-i-1]; buffer[count-i-1] = tmp; }
    buffer[count] = 0; text(x, y, buffer, color);
}
void Display::present() { __DSB(); LTDC->SRCR = LTDC_SRCR_IMR; }
void Display::progress(const char* stage, uint32_t done, uint32_t total) {
    rectangle(8, 166, 304, 48, Background);
    text(8, 168, stage, Cyan);
    rectangle(8, 184, 304, 10, Selected);
    const uint32_t percent = total ? uint64_t(done) * 100 / total : 0;
    rectangle(8, 184, percent > 100 ? 304 : percent * 304 / 100, 10, Green);
    number(8, 204, percent); text(30, 204, "/100");
    boot_diagnostics.progress = percent;
    present();
    Watchdog::progress(); // Recovery's progress screens report real steps (update, backup, FAT check).
    Board::service();
}
}

