#include "openosc/platform/board.hpp"
#ifdef OPENOSC_APPLICATION
#include "openosc/display/display.hpp"
#endif
#include "openosc/platform/clocks.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/platform/dma_memory.hpp"
#include "openosc/platform/watchdog.hpp"
#ifdef OPENOSC_APPLICATION
#include "openosc/board/current.hpp"
#include "openosc/platform/input_service.hpp"
#include "openosc/platform/usb_core.hpp"
#endif
uint32_t SystemCoreClock = 64000000U;
uint32_t SystemD2Clock = 64000000U;
extern "C" const uint8_t D1CorePrescTable[16] = {0,0,0,0,1,2,3,4,1,2,3,4,6,7,8,9};
extern "C" { volatile openosc::Diagnostics boot_diagnostics = {}; }

namespace openosc {
#ifdef OPENOSC_APPLICATION
extern "C" char __openosc_framebuffer[]; // ld/application.ld: framebuffer 0.
static bool splash_ = false;
static uint32_t entry_[Board::DisplayEntryWords]{};
bool Board::splash() { const bool kept = splash_; splash_ = false; return kept; } // Once.
const uint32_t *Board::displayAtEntry() { return entry_; }
#else
bool Board::splash() { return false; }
const uint32_t *Board::displayAtEntry() { static const uint32_t none[DisplayEntryWords]{}; return none; }
#endif
bool Board::initialize() {
    boot_diagnostics.magic = 0x4f534342;
    boot_diagnostics.stage = 1;
    // First: from here on this image owns the watchdog (docs/WATCHDOG.md), and the previous run's
    // cause is read before anything else can overwrite it.
    Watchdog::start();
#ifdef OPENOSC_APPLICATION
    ResetDiagnostics::capture(true);
#else
    ResetDiagnostics::capture(false);
#endif
    // Discard interrupt configuration inherited from the previous firmware.
    for (unsigned n = 0; n < 8; ++n) { NVIC->ICER[n] = 0xffffffff; NVIC->ICPR[n] = 0xffffffff; }
    SysTick->CTRL = 0;
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE(); __HAL_RCC_GPIOE_CLK_ENABLE();
    // Quiesce factory acquisition before reusing RAM and changing clocks.
    for (unsigned controller = 0; controller < 2; ++controller) {
        const uint32_t base = 0x40020000U + controller * 0x400;
        for (unsigned stream = 0; stream < 8; ++stream)
            *reinterpret_cast<volatile uint32_t*>(base + 0x10 + stream * 0x18) &= ~1U;
    }
    TIM1->CR1 &= ~TIM_CR1_CEN; TIM3->CR1 &= ~TIM_CR1_CEN; TIM14->CR1 &= ~TIM_CR1_CEN;
#ifdef OPENOSC_APPLICATION
    // The output-enable state is never restored across reset.
    // The ZT706S uses PA6 for something else (the power key); its generator is a USART3 module.
    // generatorPin stays at function scope: narrowing it changes the ZT703S stack layout.
    GPIO_InitTypeDef generatorPin;
    if (board::Current::Features.generator == board::GeneratorDriver::Dac) {
        __HAL_RCC_TIM6_CLK_ENABLE(); __HAL_RCC_TIM13_CLK_ENABLE();
        __HAL_RCC_DAC2_CLK_ENABLE(); __HAL_RCC_BDMA2_CLK_ENABLE();
        TIM6->CR1 = 0; TIM13->CR1 = 0; TIM13->CCER = 0; DAC2->CR = 0;
        BDMA2_Channel0->CCR &= ~BDMA_CCR_EN;
        generatorPin = {}; generatorPin.Pin=GPIO_PIN_6; generatorPin.Mode=GPIO_MODE_ANALOG;
        HAL_GPIO_Init(GPIOA,&generatorPin);
    }
    // Factory/Recovery may hand over a running OTG1 core with D+ pulled up; a boot on a profile
    // without USB data (ClockPolicy) or with the USB controller disabled in Settings never starts
    // its own. Once, before any USB attach (Registers only: usbPcd is not set up yet).
    UsbCore::powerOff();
#endif
#ifdef OPENOSC_APPLICATION
    entry_[1] = LTDC->GCR; entry_[2] = LTDC_Layer1->CR; entry_[3] = LTDC_Layer1->CFBAR;
    entry_[4] = LTDC_Layer1->PFCR; entry_[5] = LTDC_Layer1->CFBLR; entry_[6] = LTDC_Layer2->CR;
    // Frame 0 holds the logo (one layer, our address, RGB565, 320 px); else stopped until Display resets it.
    splash_ = (entry_[1] & LTDC_GCR_LTDCEN) && (entry_[2] & LTDC_LxCR_LEN) && !(entry_[6] & LTDC_LxCR_LEN) &&
              entry_[3] == uint32_t(reinterpret_cast<uintptr_t>(__openosc_framebuffer)) &&
              (entry_[4] & 7U) == LTDC_PIXEL_FORMAT_RGB565 && ((entry_[5] >> 16) & 0x1fffU) == 640U;
    entry_[0] = splash_;
    LTDC->GCR &= ~LTDC_GCR_LTDCEN; // Not scanning while the clocks change (stopped, the panel shows black).
#endif
    HAL_Init();
    __enable_irq();
    if (!ClockControl::configure()) { boot_diagnostics.error = 1; return false; }
#ifdef OPENOSC_APPLICATION
    DmaMemory::initialize();
    // The logo in frame 0 again, on this image's own timings and layer set-up, while storage mounts.
    if (splash_ && !Display::restart(__openosc_framebuffer)) { splash_ = false; entry_[0] |= 2U; }
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    *reinterpret_cast<volatile uint32_t*>(0xe0001fb0) = 0xc5acce55; // Cortex-M7 DWT lock access.
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
#endif
    boot_diagnostics.stage = 2;
    Keypad::initialize();
    return true;
}

// Servicing while waiting: not a proof of progress (Watchdog::heartbeat/progress are).
void Board::service() {
    Watchdog::service(HAL_GetTick(), reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
    ++boot_diagnostics.heartbeat;
}
uint32_t Board::milliseconds() { return HAL_GetTick(); }
void Board::delay(uint32_t ms) {
    const auto start = milliseconds();
    while (milliseconds() - start < ms) service();
}
}
extern "C" void SysTick_Handler() {
    HAL_IncTick();
#ifdef OPENOSC_APPLICATION
    openosc::InputService::tick(HAL_GetTick());
#endif
}
extern "C" void HAL_Delay(uint32_t ms) { openosc::Board::delay(ms); }
