#include "openosc/display/boot_splash.hpp"
#include "openosc/display/display.hpp"
#include "openosc/platform/board.hpp"
#include "boot_splash_images.hpp"

namespace openosc::boot_splash {
namespace {
static_assert(sizeof Images / sizeof *Images == unsigned(Style::Count), "boot style table mismatch");
// The handoff is at 0x2001ff40 and the reset record begins at 0x2001ff80.
volatile uint32_t *const Seed = reinterpret_cast<volatile uint32_t*>(0x2001ff58);

uint32_t randomWord() {
    // HSI48 is running before any splash. This switches only the RNG's selector.
    const auto previousClock = __HAL_RCC_GET_RNG_SOURCE();
    __HAL_RCC_RNG_CONFIG(RCC_RNGCLKSOURCE_HSI48);
    __HAL_RCC_RNG_CLK_ENABLE();
    __HAL_RCC_RNG_FORCE_RESET();
    __HAL_RCC_RNG_RELEASE_RESET();
    RNG->CR = RNG_CR_RNGEN;
    const auto started = Board::milliseconds();
    uint32_t status;
    do {
        status = RNG->SR;
        if (status & (RNG_SR_DRDY | RNG_SR_CECS | RNG_SR_SECS | RNG_SR_CEIS | RNG_SR_SEIS)) break;
        Board::service();
    } while (Board::milliseconds() - started < 3);
    constexpr uint32_t errors = RNG_SR_CECS | RNG_SR_SECS | RNG_SR_CEIS | RNG_SR_SEIS;
    const bool ready = (status & RNG_SR_DRDY) && !(status & errors);
    uint32_t word = ready ? RNG->DR : SysTick->VAL ^ *reinterpret_cast<const uint32_t*>(UID_BASE);
    RNG->CR = 0;
    __HAL_RCC_RNG_CLK_DISABLE();
    __HAL_RCC_RNG_CONFIG(previousClock);
    if (!ready) {
        if (Seed[0] == ~Seed[1]) word ^= Seed[0];
        if (!word) word = 0x9e3779b9;
        word ^= word << 13;
        word ^= word >> 17;
        word ^= word << 5;
    }
    Seed[0] = word;
    Seed[1] = ~word;
    return word;
}
} // namespace

Style choose() {
    return Style((uint64_t(randomWord()) * unsigned(Style::Count)) >> 32);
}

void draw(Style style, uint16_t *frame) {
    const auto &image = Images[valid(style) ? unsigned(style) : 0];
    auto *cursor = image.commands;
    const auto *end = cursor + image.size;
    unsigned written = 0, nextService = 1024;
    while (cursor != end) {
        const uint8_t command = *cursor++;
        unsigned count = (command & 0x3f) + 1;
        if (command & 0x80) {
            while (count--) { frame[written] = frame[written - Display::Width]; ++written; }
        } else if (command & 0x40) {
            const auto color = image.colors[*cursor++];
            while (count--) frame[written++] = color;
        } else {
            while (count--) frame[written++] = image.colors[*cursor++];
        }
        if (written >= nextService) { Board::service(); nextService += 1024; }
    }
    __DSB();
}
} // namespace openosc::boot_splash
