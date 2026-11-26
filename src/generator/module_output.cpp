#include "openosc/generator/module_output.hpp"
#if OPENOSC_BOARD == 706
#include "openosc/generator/capabilities.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/interrupts.hpp"

namespace openosc::generator {
namespace {
// What the USART3 interrupt shares with the foreground: the frame on the wire, its completion count
// (epoch: one per transmission complete) and the bytes received, each with the epoch it came in.
struct Received { uint8_t byte; uint32_t epoch; };
struct Wire {
    const uint8_t *volatile data = nullptr;
    volatile uint16_t index = 0, length = 0;
    volatile uint32_t epoch = 0;
    Received received[16]{};
    volatile uint8_t head = 0, tail = 0; // Written by the interrupt / by the foreground.
};
Wire wire;
// The Arbitrary waveform's banks (ArbitraryBanks): 2 x 510 bytes in SRAM4 (.generator, the DAC tables'
// section on the ZT703S, otherwise unused on this board), outside the DTCM that holds the stack.
alignas(32) uint16_t tables[2][ModuleArbitraryLimits::MaximumPoints] __attribute__((section(".generator")));
static_assert(sizeof tables == 2 * sizeof(ArbitraryBanks::Table), "One bank a table of the board's limits");
constexpr uint32_t BaudRate = 9600;
constexpr unsigned Priority = 7; // Below the capture DMA (5) and USB (4): a byte has ~1 ms.
constexpr uint32_t Errors = USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE;
void interrupt() {
    const uint32_t control = USART3->CR1, status = USART3->ISR;
    // The frame's end first: TCIE was armed in an earlier interrupt (after the last TDR write), so
    // TC set now is that frame's real end. A byte pending in the same pass is then tagged with the
    // completed frame, as its acknowledge must be; tagged before the TC it was dropped as stale
    // (issue #3). A byte that came during the transmission was taken by an earlier RXNE interrupt.
    if ((control & USART_CR1_TCIE) && (status & USART_ISR_TC)) {
        USART3->CR1 &= ~USART_CR1_TCIE; USART3->ICR = USART_ICR_TCCF; wire.epoch = wire.epoch + 1;
    }
    if (status & USART_ISR_RXNE_RXFNE) {
        const uint8_t byte = uint8_t(USART3->RDR);
        const uint8_t next = uint8_t((wire.head + 1) % (sizeof wire.received / sizeof *wire.received));
        // A byte with a framing, noise or parity error (or after an overrun) is no acknowledge.
        if (!(status & Errors) && next != wire.tail) { wire.received[wire.head] = {byte, wire.epoch}; wire.head = next; }
    }
    if (status & Errors) USART3->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
    if ((control & USART_CR1_TXEIE_TXFNFIE) && (status & USART_ISR_TXE_TXFNF)) {
        if (wire.index < wire.length) USART3->TDR = wire.data[wire.index++];
        if (wire.index >= wire.length) { // The last byte is in: TC from before it means nothing.
            USART3->ICR = USART_ICR_TCCF;
            USART3->CR1 = (USART3->CR1 & ~USART_CR1_TXEIE_TXFNFIE) | USART_CR1_TCIE;
        }
    }
}
} // namespace

ModuleOutput::ModuleOutput(PeripheralResources &) : banks_(tables) {}
bool ModuleOutput::start() {
    if (running_) return true;
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();
    // The kernel mux is shared with UART5 (DMM), which selects PCLK1 too.
    RCC_PeriphCLKInitTypeDef clock{};
    clock.PeriphClockSelection = RCC_PERIPHCLK_USART3;
    clock.Usart234578ClockSelection = RCC_USART234578CLKSOURCE_D2PCLK1;
    GPIO_InitTypeDef pins{};
    pins.Pin = GPIO_PIN_10 | GPIO_PIN_11; pins.Mode = GPIO_MODE_AF_PP; pins.Pull = GPIO_PULLUP;
    pins.Speed = GPIO_SPEED_FREQ_LOW; pins.Alternate = GPIO_AF7_USART3;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) { portFailed_ = true; return false; }
    HAL_GPIO_Init(GPIOC, &pins);
    USART3->CR1 = 0;
    USART3->CR2 = 0; USART3->CR3 = 0; USART3->PRESC = 0;
    USART3->BRR = (HAL_RCC_GetPCLK1Freq() + BaudRate / 2) / BaudRate;
    USART3->ICR = 0xffffffffU;
    wire.data = nullptr; wire.index = wire.length = 0; wire.tail = wire.head;
    if (!Interrupts::attach(USART3_IRQn, interrupt, Priority)) { portFailed_ = true; return false; }
    USART3->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE_RXFNEIE | USART_CR1_UE;
    running_ = true; portFailed_ = false; onWire_ = false;
    return true;
}
void ModuleOutput::stop() {
    if (!running_) return;
    Interrupts::detach(USART3_IRQn);
    USART3->CR1 = 0;
    wire.data = nullptr; wire.length = 0; onWire_ = false;
    running_ = false;
}
bool ModuleOutput::request(bool on) {
    if (!submit(on)) return false; // Refused: what was pending before (and its recovery) stays.
    recovery_.reset();
    return true;
}
void ModuleOutput::failed(uint32_t now) {
    applied_ = Applied::Unknown;
    recovery_.failed(now);
}
bool ModuleOutput::frame(const Settings &settings, bool on, ModuleWave::Frame &frame) const {
    if (on && settings.arbitrary())
        return ModuleWave::buildArbitrary(banks_.samples(), banks_.points(), settings.arbitrarySamplesPerSecond, frame);
    return ModuleWave::build(settings, on, frame);
}
bool ModuleOutput::submit(bool on) {
    ModuleWave::Frame frame;
    if (!this->frame(settings_, on, frame) || !start()) return false;
    if (on) link_.submit(frame); else link_.submitOff(frame);
    return true;
}
bool ModuleOutput::configure(const Settings &settings) {
    if (!ModuleCapabilities::accepts(settings)) return false;
    if (settings.arbitrary() && !banks_.loaded()) {
        // Selected with no waveform (after a reset): kept as asked, off until one is committed. Not
        // while the output is on: what plays stays.
        if (requested_) return false;
        settings_ = settings; plan_ = {};
        return true;
    }
    ModuleWave::Frame frame;
    if (!this->frame(settings, true, frame)) return false;
    const auto previous = settings_;
    settings_ = settings;
    if (requested_ && !settings_.same(previous) && !request(true)) { settings_ = previous; return false; }
    planned(frame);
    return true;
}
ModuleOutput::Commit ModuleOutput::commitArbitrary(const void *owner, unsigned points, ArbitrarySource source) {
    ArbitraryBanks::Undo undo;
    if (!banks_.commit(owner, points, source, undo)) { banks_.abort(owner); return Commit::Refused; }
    if (!settings_.arbitrary()) return Commit::Done; // Kept for when Arbitrary is selected.
    ModuleWave::Frame frame;
    // Every code was checked as it came (ArbitraryBanks::put) and the rate by configure(): it builds.
    if (this->frame(settings_, true, frame) && (!requested_ || request(true))) { planned(frame); return Commit::Done; }
    // ON and the UART did not start: nothing was sent, the previous waveform stays the loaded one.
    banks_.rollback(undo);
    return Commit::Refused;
}
bool ModuleOutput::enable() {
    if (settings_.arbitrary() && !banks_.loaded()) return false; // Nothing to play (after a reset).
    // Already on or on its way; after a failure (NO ACK, stuck UART) ON is asked again.
    if (requested_ && (link_.busy() || applied_ == Applied::On)) return true;
    if (!request(true)) return false;
    requested_ = true;
    return true;
}
void ModuleOutput::disable() {
    if (!requested_ && (applied_ == Applied::Off || link_.offQueued())) return;
    // OFF is the intent even when the UART does not start: recovery sends OFF later, never ON again.
    requested_ = false;
    if (!request(false)) { recovery_.reset(); failed(Board::milliseconds()); }
}
void ModuleOutput::service() {
    const uint32_t now = Board::milliseconds();
    // The latest requested state again after a failure; a stopped UART starts anew for it.
    if (recovery_.due(now) && !submit(requested_)) failed(now);
    if (!running_) return;
    // Completion first: an acknowledge already received in the same pass then counts.
    if (onWire_ && wire.epoch != sentEpoch_) { sentEpoch_ = wire.epoch; onWire_ = false; wire.length = 0; link_.sent(now); }
    while (wire.tail != wire.head) {
        const Received r = wire.received[wire.tail];
        wire.tail = uint8_t((wire.tail + 1) % (sizeof wire.received / sizeof *wire.received));
        if (r.epoch == sentEpoch_ && !onWire_ && link_.received(r.byte)) {
            applied_ = link_.currentOff() ? Applied::Off : Applied::On;
            recovery_.reset();
        }
    }
    switch (link_.service(now)) {
    case ModuleLink::Failure::Stuck: failed(now); stop(); return;
    case ModuleLink::Failure::Timeout: failed(now); break;
    case ModuleLink::Failure::None: break;
    }
    if (link_.due() && !onWire_) {
        const auto &frame = link_.begin(now);
        applied_ = Applied::Unknown; // On its way: the module's state is not known until acknowledged.
        sentEpoch_ = wire.epoch; onWire_ = true;
        wire.data = frame.bytes; wire.index = 0; wire.length = SerialFrameCodec::Bytes;
        USART3->CR1 |= USART_CR1_TXEIE_TXFNFIE;
    }
}
bool ModuleOutput::suspendForClock() {
    if (clockBusy()) return false; // DataAccess refuses before this; a frame is never cut.
    link_.interrupt();
    stop();
    return true;
}
bool ModuleOutput::retime(bool) {
    if (!link_.busy() && applied_ != Applied::Unknown && !requested_ && !portFailed_) return true; // Off and idle: no UART needed.
    return start(); // The frame waiting goes on the new clock (service).
}
} // namespace openosc::generator
#endif
