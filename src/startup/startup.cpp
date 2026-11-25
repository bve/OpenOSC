#include "openosc/platform/board.hpp"
#include "openosc/platform/reset_record.hpp"
#include "openosc/platform/system_reset.hpp"
extern "C" {
extern uint32_t _estack, _sitext, _stext, _etext, _sidata, _sdata, _edata, _sbss, _ebss;
#ifdef OPENOSC_APPLICATION
extern uint32_t _sidma_data, _sdma_data, _edma_data;
#endif
#ifdef OPENOSC_APPLICATION
// ITCM workspaces using 64-bit ECC words.
extern uint64_t _sdecode_workspace, _edecode_workspace, _susb_trace, _eusb_trace;
extern uint64_t _sbode, _ebode, _sscpi_measure, _escpi_measure, _sdmm_log, _edmm_log;
#endif
void Reset_Handler();
void Bootstrap_Fault();
void Default_Handler();
void HardFault_Handler();
void SysTick_Handler();
void OTG_HS_IRQHandler();
int main();
void __libc_init_array();
void _init() {}
#ifdef OPENOSC_APPLICATION
// Its own section at the start of DTCM (application.ld): inside .bss its 1 KiB alignment left holes of up
// to 1 KiB wherever the linker placed it. Not zeroed with .bss: Reset_Handler writes every entry.
#define OPENOSC_RUNTIME_VECTORS __attribute__((section(".runtime_vectors"), aligned(1024)))
#else
#define OPENOSC_RUNTIME_VECTORS __attribute__((aligned(1024)))
#endif
OPENOSC_RUNTIME_VECTORS uint32_t runtime_vectors[256];
extern const uint32_t vectors[171]; // The runtime handlers (vectors.S), relocated with the code.
// Every handler of image_vectors (vectors.S): only taken before Reset_Handler switches VTOR to
// runtime_vectors, and only when the loader pointed VTOR at this image (our Handoff does; the factory
// boot leaves its own). It lies in the image itself and calls nothing, so it never runs relocated
// code that is not copied yet: a reset request; the IWDG ends it if the request does not.
__attribute__((section(".bootstrap"), noinline, noreturn, used)) void Bootstrap_Fault() {
    __DSB();
    SCB->AIRCR = (0x5FAUL << SCB_AIRCR_VECTKEY_Pos) | (SCB->AIRCR & SCB_AIRCR_PRIGROUP_Msk) | SCB_AIRCR_SYSRESETREQ_Msk;
    __DSB();
    for (;;) {}
}
__attribute__((section(".bootstrap"), noinline, optimize("no-tree-loop-distribute-patterns")))
void Reset_Handler() {
    __disable_irq();
    SCB->CPACR |= 0x00f00000;
    __DSB(); __ISB();
    if (SCB->CCR & SCB_CCR_DC_Msk) SCB_DisableDCache();
    if (SCB->CCR & SCB_CCR_IC_Msk) SCB_DisableICache();
    MPU->CTRL = 0;
    __DSB(); __ISB();
#ifndef OPENOSC_APPLICATION
    auto* source = &_sitext;
    for (auto* p = &_stext; p < &_etext; ++p) *p = *source++;
#endif
    auto* sourceData = &_sidata;
    for (auto* p = &_sdata; p < &_edata; ++p) *p = *sourceData++;
#ifdef OPENOSC_APPLICATION
    auto* sourceDma = &_sidma_data;
    for (auto* p = &_sdma_data; p < &_edma_data; ++p) *p = *sourceDma++;
#endif
    for (auto* p = &_sbss; p < &_ebss; ++p) *p = 0;
#ifdef OPENOSC_APPLICATION
    // Before any constructor (the decoder's job lives there) or trace hook: ITCM enabled, with
    // read-modify-write for its ECC (byte and halfword stores are narrower than an ECC word), and
    // every ECC word of its sections written once in 64-bit stores.
    SCB->ITCMCR |= SCB_ITCMCR_EN_Msk | SCB_ITCMCR_RMW_Msk;
    __DSB(); __ISB();
    for (auto* p = &_sdecode_workspace; p < &_edecode_workspace; ++p) *p = 0;
    for (auto* p = &_susb_trace; p < &_eusb_trace; ++p) *p = 0;
    for (auto* p = &_sbode; p < &_ebode; ++p) *p = 0;
    for (auto* p = &_sscpi_measure; p < &_escpi_measure; ++p) *p = 0;
    for (auto* p = &_sdmm_log; p < &_edmm_log; ++p) *p = 0;
    __DSB();
#endif
    for (unsigned i = 0; i < 256; ++i)
        runtime_vectors[i] = i < 171 ? reinterpret_cast<const uint32_t*>(vectors)[i] : 0;
    SCB->VTOR = reinterpret_cast<uint32_t>(runtime_vectors);
    __DSB(); __ISB();
    __libc_init_array();
    main();
    for (;;) {}
}
// Fatal exceptions (docs/WATCHDOG.md): record why, then reset at once. Nothing here calls a function
// or feeds a watchdog; if the reset request itself failed, the unfed IWDG resets within its timeout,
// and a fault inside this code locks the core up, which the IWDG ends the same way.
namespace {
using openosc::ResetRecord;
__attribute__((always_inline)) inline void record(ResetRecord::Cause cause, uint32_t pc, uint32_t stackedLr,
                                                  uint32_t excReturn, uint32_t detail) {
    volatile ResetRecord &r = ResetRecord::retained();
    r.cause = uint32_t(cause);
#ifdef OPENOSC_APPLICATION
    r.source = uint32_t(ResetRecord::Source::Application);
#else
    r.source = uint32_t(ResetRecord::Source::Recovery);
#endif
    r.pc = pc; r.lr = stackedLr; r.excReturn = excReturn;
    r.cfsr = SCB->CFSR; r.hfsr = SCB->HFSR; r.detail = detail;
    ResetRecord::seal(r);
}
} // namespace
void Default_Handler() {
    const uint32_t vector = SCB->ICSR & 0x1ff;
    openosc::boot_diagnostics.error = 0x10000000 | vector;
    record(ResetRecord::Cause::Interrupt, 0, 0, 0, vector);
    openosc::SystemReset::now();
}
// The stacked frame is read only within DTCM (ExceptionFrame): a corrupt stack pointer leaves PC/LR 0
// and detail 1 instead of faulting again.
__attribute__((used)) void fault_capture(uint32_t* frame, uint32_t lr) {
    const bool readable = openosc::ExceptionFrame::readable(reinterpret_cast<uintptr_t>(frame));
    const uint32_t pc = readable ? frame[6] : 0, stackedLr = readable ? frame[5] : 0;
    openosc::boot_diagnostics.stage = 0xdead;
    openosc::boot_diagnostics.faultPc = pc;
    openosc::boot_diagnostics.faultLr = lr;
    openosc::boot_diagnostics.faultCfsr = SCB->CFSR;
    record(ResetRecord::Cause::Fault, pc, stackedLr, lr, readable ? 0 : 1);
    openosc::SystemReset::now();
}
__attribute__((naked)) void HardFault_Handler() {
    __asm volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n mov r1, lr\n b fault_capture");
}
}

