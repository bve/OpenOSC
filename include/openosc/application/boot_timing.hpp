#pragma once
#include "openosc/platform/board.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace openosc {
// Milliseconds at each boot stage of the application, the first of each (SYST:BOOT?).
struct BootTiming {
    // Crc..Fat (f_mount) fall before Mounted; listed last so older fields keep their places.
    enum Stage : unsigned { Start, Mounted, Settings, Guard, Clocks, Data, Display, View, Frame, Crc, Nor, Disk, Fat, Count };
    static inline uint32_t at[Count]{};
    static inline uint16_t marked = 0;
    static void mark(Stage stage) {
        if (marked & (1U << stage)) return;
        at[stage] = Board::milliseconds(); marked |= uint16_t(1U << stage);
    }
    // "start,mounted,...,frame,crc,nor,disk,fat" in ms (Stage order; an unreached one empty).
    static size_t text(char *out, size_t size) {
        size_t n = 0;
        for (unsigned i = 0; i < Count && n < size; ++i) {
            const int m = (marked & (1U << i)) ? std::snprintf(out + n, size - n, "%s%lu", i ? "," : "", (unsigned long)at[i])
                                               : std::snprintf(out + n, size - n, "%s", i ? "," : "");
            if (m < 0) break;
            n += size_t(m);
        }
        return n < size ? n : size - 1;
    }
};
} // namespace openosc
