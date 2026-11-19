#pragma once
#include <cstdint>

namespace openosc::dmm {
// The DMM chip's supervision, decided here and carried out by DataAccess (no hardware in this class):
// - a chip silent for SilentUs (no framed packet) is power-cycled: PowerOff, then PowerOn after OffMs,
//   the silence doubled per futile cycle up to 2^MaxDoublings; any packet resets it;
// - the chip always gets OffMs unpowered between two power-ons, also when the DMM is left and entered
//   again quickly (activate() then keeps it off for the rest);
// - a UART in HardwareError (a DMA transfer error: UartDma never restarts itself) is restarted in the
//   foreground at most UartRetries times, UartRetryMs doubled per try, never while a command transmits
//   (the restart waits for its end, no retry spent); past that the silence rule
//   power-cycles. A packet or a power-on gives the retries again, so a fault that persists costs at most
//   UartRetries restarts per power cycle and the cycles back off.
// The first packet after a power cycle counts a restart (the chip then runs its default function);
// one after reselect() a reselect (a failed MODE change that powered it again, no notice).
class LinkWatchdog {
public:
    static constexpr uint64_t SilentUs = 5000000;
    static constexpr unsigned MaxDoublings = 4; // Up to 80 s between futile cycles.
    static constexpr uint32_t OffMs = 1000;
    static constexpr unsigned UartRetries = 3;
    static constexpr uint32_t UartRetryMs = 100; // 100, 200, 400 ms.
    enum class Action : uint8_t { None, PowerOff, PowerOn, RestartUart };

    void left(uint32_t nowMs) { // The DMM's word was replaced: the chip lost its power now (unless already off).
        if (off_) return;
        leftAt_ = nowMs;
        left_ = true;
    }
    // The DMM activated: true while the chip has to stay unpowered (remainingMs of OffMs left); else a
    // fresh supervision from nowUs.
    bool activate(uint32_t nowMs, uint64_t nowUs, uint32_t &remainingMs);
    void deactivate(); // Another view: nothing supervised, the counters go.
    Action service(uint32_t nowMs, uint64_t nowUs, uint64_t activatedUs, bool uartFault, bool txBusy);
    void stopFailed(uint64_t nowUs); // PowerOff could not stop the UART: still powered, silence counted again.
    void packet(uint64_t nowUs);
    void reselect() { reselect_ = true; }
    bool off() const { return off_; }
    uint32_t restarts() const { return restarts_; }
    uint32_t reselects() const { return reselects_; }
    uint32_t uartRestarts() const { return uartRestarts_; }
    uint32_t powerCycles() const { return powerCycles_; }
    uint64_t silence() const { return SilentUs << (futile_ < MaxDoublings ? futile_ : MaxDoublings); }

private:
    bool off_ = false, left_ = false, tripped_ = false, recovering_ = false, reselect_ = false, faulted_ = false;
    uint32_t leftAt_ = 0, offAt_ = 0, retryAt_ = 0;
    unsigned futile_ = 0, uartRetries_ = 0;
    uint32_t restarts_ = 0, reselects_ = 0, uartRestarts_ = 0, powerCycles_ = 0;
    uint64_t packetUs_ = 0;
};
} // namespace openosc::dmm
