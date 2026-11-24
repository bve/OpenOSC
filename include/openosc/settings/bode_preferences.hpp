#pragma once
#include <cstdint>

namespace openosc {
// Analyze > Bode setup (GitHub #124): the choices a sweep starts from, kept in settings.json (scope.bode)
// and saved with the other settings at power-off. The user's choices only: a sweep's points and results
// stay in its session. Compact (Preferences sits on several stacks): the frequencies as steps, the lists by index.
// - start, stop: an index into Steps, or Limit: the highest sine the drive plays (it differs by board,
//   level, amplitude and the DAC's clock, so the choice itself is kept).
//   A saved step above today's limit plays at the limit and stays saved as it was.
// - density, settle, averages: indices into scope::BodeSettings' PointsChoices, SettleChoices, AverageChoices.
// - drive: ui::BodeDrive (From GEN, Dedicated, Low, High, Custom); a board without levels (ZT706S) plays
//   From GEN whatever is saved. customLow < customHigh <= CodeMax: the Custom drive's DAC codes.
struct BodePreferences {
    // The frequencies offered for the start and the stop (millihertz): 1-2-5 from 1 Hz, those below the
    // drive's limit offered, then the limit. The ZT703S's limit is below 500 kHz (DacCapabilities: 344 kHz
    // at most), so the steps above it are the module's.
    static constexpr uint64_t Steps[] = {1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000, 500000, 1000000,
                                         2000000, 5000000, 10000000, 20000000, 50000000, 100000000, 200000000,
                                         500000000, 1000000000, 2000000000, 5000000000};
    static constexpr uint8_t StepCount = sizeof Steps / sizeof *Steps, Limit = StepCount;
    static constexpr uint8_t Densities = 4, Settles = 8, Averages = 3, Drives = 5; // The lists' lengths.
    static constexpr uint16_t CodeMax = 4095;
    uint8_t start = 6, stop = 15;                   // 100 Hz .. 100 kHz.
    uint8_t density = 1, settle = 2, averages = 0;  // 10 a decade, 50 ms, 1 record.
    uint8_t drive = 0;                              // From GEN.
    uint16_t customLow = 0, customHigh = 1600;      // The Low level's span.
    static bool frequency(uint8_t index) { return index <= Limit; }
    static bool codes(uint32_t low, uint32_t high) { return low < high && high <= CodeMax; }
    bool valid() const {
        return frequency(start) && frequency(stop) && density < Densities && settle < Settles && averages < Averages &&
               drive < Drives && codes(customLow, customHigh);
    }
    bool operator==(const BodePreferences &o) const {
        return start == o.start && stop == o.stop && density == o.density && settle == o.settle && averages == o.averages &&
               drive == o.drive && customLow == o.customLow && customHigh == o.customHigh;
    }
    bool operator!=(const BodePreferences &o) const { return !(*this == o); }
};
} // namespace openosc
