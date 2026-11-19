#include "openosc/platform/watchdog.hpp"
#include "openosc/platform/diagnostics.hpp"
#include "openosc/platform/reset_record.hpp"
#include "openosc/platform/system_reset.hpp"
#include "stm32h7xx.h"

extern "C" uint32_t HAL_GetTick(void);

namespace openosc {
namespace {
#ifdef OPENOSC_APPLICATION
constexpr ResetRecord::Source ThisImage = ResetRecord::Source::Application;
#else
constexpr ResetRecord::Source ThisImage = ResetRecord::Source::Recovery;
#endif
constexpr uint32_t KeyStart = 0xcccc, KeyAccess = 0x5555, KeyRefresh = 0xaaaa;
WatchdogPolicy policy_;
uint32_t status_ = 0, fedAt_ = 0; // fedAt_: the ms tick of the last IWDG reload by service().

// Thread mode with interrupts enabled and unmasked by priority: the main loop or a wait inside it,
// never an interrupt.
bool threadContext() { return !__get_IPSR() && !__get_PRIMASK() && !__get_BASEPRI(); }
bool updated() {
    for (uint32_t poll = 0; poll < Watchdog::UpdatePolls; ++poll)
        if (!(IWDG1->SR & (IWDG_SR_PVU | IWDG_SR_RVU | IWDG_SR_WVU))) return true;
    return false;
}
// A window watchdog the factory or a previous image enabled cannot be stopped: refreshed on the same
// policy terms as the IWDG but at every service (its window may be far shorter than a ms), only while
// it would accept the refresh (inside its window).
void refreshWindow() {
    const uint32_t counter = WWDG1->CR;
    if ((counter & WWDG_CR_WDGA) && ((counter & WWDG_CR_T) <= (WWDG1->CFR & WWDG_CFR_W))) WWDG1->CR = WWDG_CR_T | WWDG_CR_WDGA;
}
void record(ResetRecord::Cause cause, uint32_t pc, uint32_t detail) {
    auto &r = ResetRecord::retained();
    r.cause = uint32_t(cause); r.source = uint32_t(ThisImage); r.pc = pc; r.lr = 0; r.excReturn = 0;
    r.cfsr = SCB->CFSR; r.hfsr = SCB->HFSR; r.detail = detail;
    ResetRecord::seal(r);
    __DSB();
}
} // namespace

void Watchdog::start() {
    status_ = 0;
    IWDG1->KR = KeyStart;   // Starts IWDG1 and the LSI; a running one (factory boot) is unaffected.
    IWDG1->KR = KeyRefresh; // A full period of the inherited settings for the takeover below.
    IWDG1->KR = KeyAccess;
    IWDG1->PR = PrescalerCode;
    IWDG1->RLR = Reload;
    bool ok = updated();
    IWDG1->WINR = Reload;   // No window; writing WINR also reloads the counter (RM0455 IWDG).
    ok = ok && updated();
    if (!ok) { // Fail closed: an unconfirmed prescaler, reload or window is not a takeover.
        status_ = UpdateTimeout | Halted; boot_diagnostics.watchdog = status_;
        policy_.halt();
        // The previous run's cause is not yet captured: kept, not overwritten.
        if (!ResetRecord::valid(ResetRecord::retained())) record(ResetRecord::Cause::InitFailure, 0, StartFailure);
        SystemReset::now();
    }
    IWDG1->KR = KeyRefresh;
    DBGMCU->APB4FZ1 |= DBGMCU_APB4FZ1_DBG_IWDG1; // Stopped while a debugger halts the core.
    status_ = Configured;
    fedAt_ = HAL_GetTick();
    policy_.begin(fedAt_);
    boot_diagnostics.watchdog = status_;
}
// The IWDG is reloaded only when the ms tick moved since its last reload: a tick that stopped (SysTick
// off, masked or broken) cannot keep it fed however fast a stuck loop services.
void Watchdog::service(uint32_t now, uintptr_t caller) {
    if (!threadContext()) return;
    if (policy_.feeds(now)) {
        if (now != fedAt_) { IWDG1->KR = KeyRefresh; fedAt_ = now; }
        refreshWindow();
        return;
    }
    if (status_ & (Stalled | Halted)) return;
    status_ |= Stalled; boot_diagnostics.watchdog = status_;
    record(ResetRecord::Cause::Stall, uint32_t(caller), policy_.sinceProgress(now));
}
void Watchdog::heartbeat() {
    if (!threadContext()) return;
    policy_.heartbeat(HAL_GetTick());
    if (status_ & Stalled) { // Progress after all, before the reset: the stall record no longer holds.
        status_ &= ~Stalled; boot_diagnostics.watchdog = status_;
        ResetRecord::retained().magic = 0;
    }
}
void Watchdog::progress() {
    if (threadContext()) policy_.progress(HAL_GetTick());
}
void Watchdog::expect(uint32_t ms) {
    if (threadContext()) policy_.expect(HAL_GetTick(), ms);
}
// Never fed again: recorded, then reset by the watchdog (fatal, stackOverflow).
[[noreturn]] static void halted(ResetRecord::Cause cause, uint32_t detail) {
    policy_.halt();
    status_ |= Watchdog::Halted; boot_diagnostics.watchdog = status_;
    record(cause, 0, detail);
    while (true) __NOP();
}
[[noreturn]] void Watchdog::fatal(uint32_t error) { halted(ResetRecord::Cause::InitFailure, error); }
[[noreturn]] void Watchdog::stackOverflow(uint32_t survivedWords) { halted(ResetRecord::Cause::StackOverflow, survivedWords); }
void Watchdog::terminalFeed() {
    IWDG1->KR = KeyRefresh;
    refreshWindow();
}
uint32_t Watchdog::status() { return status_; }
const WatchdogPolicy &Watchdog::policy() { return policy_; }

void ResetDiagnostics::capture(bool consume) {
    boot_diagnostics.resetFlags = RCC->RSR;
    auto &r = ResetRecord::retained();
    if (ResetRecord::valid(r)) {
        boot_diagnostics.lastCause = r.cause; boot_diagnostics.lastSource = r.source;
        boot_diagnostics.lastPc = r.pc; boot_diagnostics.lastLr = r.lr; boot_diagnostics.lastCfsr = r.cfsr;
        boot_diagnostics.lastHfsr = r.hfsr; boot_diagnostics.lastDetail = r.detail;
    }
    if (!consume) return;
    r.magic = 0;
    RCC->RSR |= RCC_RSR_RMVF;
    __DSB();
}
} // namespace openosc
