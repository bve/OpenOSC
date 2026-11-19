#include "openosc/measurement/dmm/link_watchdog.hpp"

namespace openosc::dmm {
bool LinkWatchdog::activate(uint32_t nowMs, uint64_t nowUs, uint32_t &remainingMs) {
    remainingMs = 0;
    if (!off_ && left_ && uint32_t(nowMs - leftAt_) < OffMs) { // Back too soon: off for the rest.
        recovering_ = tripped_ = false;
        futile_ = 0;
        off_ = true;
        offAt_ = leftAt_;
    }
    if (off_) {
        const uint32_t elapsed = uint32_t(nowMs - offAt_);
        remainingMs = elapsed < OffMs ? OffMs - elapsed : 0;
        return true;
    }
    deactivate();
    packetUs_ = nowUs;
    return false;
}
void LinkWatchdog::deactivate() {
    off_ = tripped_ = recovering_ = faulted_ = false;
    futile_ = uartRetries_ = 0;
}
LinkWatchdog::Action LinkWatchdog::service(uint32_t nowMs, uint64_t nowUs, uint64_t activatedUs, bool uartFault, bool txBusy) {
    if (off_) {
        if (uint32_t(nowMs - offAt_) < OffMs) return Action::None;
        off_ = false;
        packetUs_ = nowUs;
        recovering_ = tripped_;
        tripped_ = faulted_ = false;
        uartRetries_ = 0;
        return Action::PowerOn;
    }
    if (!uartFault) faulted_ = false;
    else if (uartRetries_ < UartRetries) {
        if (!faulted_) { faulted_ = true; retryAt_ = nowMs + (UartRetryMs << uartRetries_); }
        if (int32_t(nowMs - retryAt_) >= 0) {
            // A command still transmits: the restart waits for its end (the fault and its deadline stay,
            // no retry is spent), then comes on the first pass after it.
            if (txBusy) return Action::None;
            faulted_ = false;
            ++uartRetries_;
            ++uartRestarts_;
            return Action::RestartUart;
        }
    }
    const uint64_t since = packetUs_ > activatedUs ? packetUs_ : activatedUs;
    if (nowUs < since || nowUs - since < silence() || txBusy) return Action::None;
    off_ = tripped_ = left_ = true;
    offAt_ = leftAt_ = nowMs; // A trip is a power-off: a quick return still waits OffMs.
    ++futile_;
    ++powerCycles_;
    return Action::PowerOff;
}
void LinkWatchdog::stopFailed(uint64_t nowUs) {
    off_ = tripped_ = false;
    packetUs_ = nowUs;
    --powerCycles_;
}
void LinkWatchdog::packet(uint64_t nowUs) {
    packetUs_ = nowUs;
    futile_ = uartRetries_ = 0;
    if (recovering_) { recovering_ = false; ++restarts_; }
    if (reselect_) { reselect_ = false; ++reselects_; }
}
} // namespace openosc::dmm
