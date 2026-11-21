#include "openosc/platform/usb_trace.hpp"
#ifdef OPENOSC_USB_TRACE
#include "openosc/platform/board.hpp"
#include "openosc/platform/interrupts.hpp"
#include "openosc/board/current.hpp"

namespace {
// The ring (12 KiB) is in ITCM (.usb_trace, ld/application.ld), so the trace build keeps the 48 KiB
// DTCM stack minimum of the ordinary build; Reset_Handler enables and zeroes it first. The ZT703S's
// ITCM also holds the Bode session (.bode, GitHub #72): its trace ring is half (6 KiB, 256 kept after
// a reset). scripts/usb_trace_capture.py reads the capacity from usb_trace_entries' size.
#if OPENOSC_BOARD == 703
constexpr uint32_t Capacity = 512, KeepAfterReset = 256;
#else
constexpr uint32_t Capacity = 1024, KeepAfterReset = 512;
#endif
constexpr uint32_t SampleHz = 20000, MinimumRun = 3;
static_assert(KeepAfterReset < Capacity);
constexpr uint8_t Idle = 0b110; // D+ high, D- low, USB33RDY set.
static_assert(openosc::board::Current::ChargeStatus.port == openosc::board::Port::C);
constexpr uint32_t PowerMask = (1U << 9) | openosc::board::Current::ChargeStatus.mask();
constexpr uint32_t ResetBits = USB_OTG_GINTSTS_USBRST | USB_OTG_GINTSTS_ENUMDNE | USB_OTG_GINTSTS_USBSUSP
                               | USB_OTG_GINTSTS_WKUINT;
struct Entry { uint32_t cycles, value, tag; };
}

extern "C" {
__attribute__((section(".usb_trace"), aligned(8))) volatile Entry usb_trace_entries[Capacity];
volatile uint32_t usb_trace_head = 0;
volatile uint32_t usb_trace_cycles_per_us = 0;
volatile uint32_t usb_trace_sample_hz = SampleHz;
volatile uint32_t usb_trace_freezing = 0;
volatile uint32_t usb_trace_remaining = KeepAfterReset;
volatile uint32_t usb_trace_armed = 0; // SWD can arm before enumeration accepts a command.
volatile uint32_t usb_trace_control = 0; // SWD request: 1 = rearm, 2 = freeze; IRQ clears on completion.
volatile uint32_t usb_trace_power_mask = PowerMask;
}

namespace openosc::usbtrace {
namespace {
void record(Tag tag, uint32_t value) {
    if (usb_trace_freezing && !usb_trace_remaining) return;
    auto &entry = usb_trace_entries[usb_trace_head % Capacity];
    entry.cycles = DWT->CYCCNT; entry.value = value; entry.tag = uint32_t(tag);
    ++usb_trace_head;
    if (usb_trace_freezing) usb_trace_remaining = usb_trace_remaining - 1;
}

void applyControl() {
    if (!usb_trace_control) return;
    const uint32_t state = __get_PRIMASK();
    __disable_irq();
    // Apply between records, even on an idle bus. Direct SWD writes to the two
    // freeze fields could race record() and underflow its remaining counter.
    if (usb_trace_control == 1) {
        usb_trace_head = 0;
        usb_trace_remaining = KeepAfterReset;
        usb_trace_freezing = 0;
        usb_trace_armed = 1;
        record(Tag::Power, PWR->CR3);
        record(Tag::PowerInputs, GPIOC->IDR & usb_trace_power_mask);
    } else if (usb_trace_control == 2) {
        usb_trace_freezing = 1;
        usb_trace_remaining = 0;
    }
    __DMB();
    usb_trace_control = 0;
    __set_PRIMASK(state);
}

void sample() {
    TIM7->SR = ~TIM_SR_UIF;
    applyControl();
    static uint8_t last = Idle;
    static uint32_t run = 0;
    static bool powerReady = true;
    static uint32_t previousInputs = 0;
    const uint32_t inputs = GPIOC->IDR & usb_trace_power_mask;
    if (inputs != previousInputs) { previousInputs = inputs; mark(Tag::PowerInputs, inputs); }
    const uint32_t power = PWR->CR3;
    const bool ready = (power & PWR_CR3_USB33RDY) != 0;
    // Power edges must not be filtered by MinimumRun for D-line activity.
    if (ready != powerReady) { powerReady = ready; mark(Tag::PowerTransition, power); }
    const uint8_t code = ((GPIOA->IDR >> 11) & 3U) | (ready ? 4U : 0U);
    if (code == last) {
        if (++run == MinimumRun && code != Idle) mark(Tag::LineBegin, code);
        return;
    }
    if (last != Idle && run >= MinimumRun) mark(Tag::LineEnd, last | (run << 8));
    last = code; run = 1;
}
}

void mark(Tag tag, uint32_t value) {
    const uint32_t state = __get_PRIMASK();
    __disable_irq();
    if (tag == Tag::Command) usb_trace_armed = 1;
    record(tag, value);
    __set_PRIMASK(state);
}

void irq(uint32_t masked, uint32_t raw, const uint32_t *frame) {
    if (!(masked & ResetBits)) return;
    mark(Tag::Power, PWR->CR3);
    mark(Tag::Control, reinterpret_cast<USB_OTG_DeviceTypeDef*>(USB1_OTG_HS_PERIPH_BASE + USB_OTG_DEVICE_BASE)->DCTL);
    mark(Tag::Session, USB1_OTG_HS->GOTGCTL);
    mark(Tag::Irq, masked & ResetBits);
    mark(Tag::IrqRaw, raw);
    mark(Tag::Foreground, frame[6]);
    mark(Tag::ForegroundLr, frame[5]);
    mark(Tag::ForegroundR0, frame[0]);
    mark(Tag::ForegroundR2, frame[2]);
    if ((masked & USB_OTG_GINTSTS_USBRST) && usb_trace_armed) usb_trace_freezing = 1;
}

void start() {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    usb_trace_cycles_per_us = SystemCoreClock / 1000000U;
    usb_trace_sample_hz = SampleHz; // An explicit volatile store keeps this SWD metadata through LTO/GC.
    __HAL_RCC_TIM7_CLK_ENABLE();
    TIM7->CR1 = 0;
    TIM7->PSC = 0;
    TIM7->ARR = TimerClock::apb1() / SampleHz - 1U;
    TIM7->EGR = TIM_EGR_UG;
    TIM7->SR = 0;
    TIM7->DIER = TIM_DIER_UIE;
    Interrupts::attach(TIM7_IRQn, sample, 2);
    TIM7->CR1 = TIM_CR1_CEN;
}
} // namespace openosc::usbtrace
#endif
