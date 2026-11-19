#pragma once
#include <cstdint>

namespace openosc {
// Why the previous run of our firmware ended, written just before it reset (docs/WATCHDOG.md).
// It lies in DTCM above both images' RAM (0x2001FF00) and below RecoveryRequest (0x2001FFC0): our
// images never place data there and the factory boot's stack starts at 0x2001EEF0. That the factory
// boot leaves it intact across a reset is NOT established; the record is used only when it validates.
struct ResetRecord {
    // StackOverflow: the application stack reached its guard above .bss (StackGuard).
    enum class Cause : uint32_t { None, Fault, Interrupt, InitFailure, Stall, StackOverflow };
    enum class Source : uint32_t { Recovery = 1, Application = 2 };
    static constexpr uint32_t Address = 0x2001ff80, Magic = 0x57444f47; // "WDOG"
    uint32_t magic, cause, source, pc, lr, excReturn, cfsr, hfsr, detail, check;

    // Inline only: the fault handler seals a record without calling anything.
    __attribute__((always_inline)) static inline uint32_t checksum(const volatile ResetRecord &r) {
        return ~(r.magic + r.cause * 3 + r.source * 5 + r.pc * 7 + r.lr * 11 + r.excReturn * 13 +
                 r.cfsr * 17 + r.hfsr * 19 + r.detail * 23);
    }
    __attribute__((always_inline)) static inline void seal(volatile ResetRecord &r) {
        r.magic = Magic;
        r.check = checksum(r);
    }
    static bool valid(const volatile ResetRecord &r) {
        return r.magic == Magic && r.check == checksum(r) && r.cause >= uint32_t(Cause::Fault) &&
               r.cause <= uint32_t(Cause::StackOverflow);
    }
    // Recovery's start (PR #29): Recovery itself faulted last time (a fault or an unexpected interrupt,
    // e.g. on an ECC-broken header after a cut update), so its menu comes up instead of an auto-start into
    // the same fault. Taken once: the record is consumed, the next start launches as before.
    static bool takeRecoveryFault(volatile ResetRecord &r) {
        const bool faulted = valid(r) && r.source == uint32_t(Source::Recovery) &&
                             (r.cause == uint32_t(Cause::Fault) || r.cause == uint32_t(Cause::Interrupt));
        if (faulted) r.magic = 0;
        return faulted;
    }
#if defined(__arm__) || defined(__thumb__)
    static volatile ResetRecord &retained() { return *reinterpret_cast<volatile ResetRecord *>(Address); }
#else
    static volatile ResetRecord &retained(); // Host tests provide the storage.
#endif
};
static_assert(sizeof(ResetRecord) <= 0x40, "ResetRecord must end below RecoveryRequest (0x2001FFC0)");

// A stacked exception frame is read only when all of its eight words lie in DTCM, word aligned.
struct ExceptionFrame {
    static constexpr uintptr_t Begin = 0x20000000, End = 0x20020000;
    __attribute__((always_inline)) static inline bool readable(uintptr_t frame) {
        return !(frame & 3) && frame >= Begin && frame <= End - 32;
    }
};
} // namespace openosc
