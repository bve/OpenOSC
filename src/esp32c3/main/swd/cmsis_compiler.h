#pragma once
// The selected CMSIS-DAP sources only use these compiler helpers.
#define __STATIC_FORCEINLINE static inline __attribute__((always_inline))
#define __NOP() __asm__ volatile("nop")
#define __WEAK __attribute__((weak))
