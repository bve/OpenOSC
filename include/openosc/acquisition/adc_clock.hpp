#pragma once
#include "openosc/acquisition/adc_read_phase.hpp"
#include <cstdint>

namespace openosc::scope {
// TIM1 drives the external ADCs continuously between records; TIM3 selects reads.
class AdcClock {
public:
    AdcClock() = default;
    ~AdcClock() { stop(); }
    AdcClock(const AdcClock &) = delete;
    AdcClock &operator=(const AdcClock &) = delete;
    // Starts the clock directly at the capture's plan (never faster than it) and lets it settle.
    // `interleaved` (ZT706S, InterleavedWords): CH1 (PE9) at the high polarity and CH3 (PA10) at the
    // low one, so the two ADCs convert half a period apart (factory rearm after 0804A9A8 sets CC1P
    // only for the non-interleaved flag); otherwise both at the read phase's common polarity.
    void start(uint16_t prescaler, uint16_t period, uint32_t timerHz, bool interleaved = false);
    void stop();
    // One conversion per sample, as the factory clocks the ADCs (zt_fw sample_clocks.md): TIM1 on
    // TIM3's prescaler with one ADC period per read period (ARR period-1, CCR1/3 compare(period)), so
    // the ADCs never run faster than the record's rate. At the fastest timebase that is f_tim / the
    // profile's ADC divider: 140 MHz at 280 MHz (period 2, 50 %), 90 MHz at 360 MHz (period 4,
    // compare 2: 5.56/5.56 ns); a divider-3 profile would run period 3, compare 1 (33/67 %).
    // The output polarity sets the read phase (AdcReadPhase for this timer clock). A changed rate,
    // compare or phase is applied at once and settles before the reads start.
    void configure(uint16_t prescaler, uint16_t period, uint32_t timerHz, bool interleaved = false);
    static uint32_t outputs(uint32_t timerHz, uint16_t prescaler, uint16_t period, bool interleaved);
    // TIM1 compare (CCR1/CCR3): half of a fast ADC period, rounded down (period 2: 1, period 3: 1,
    // period 4: 2); the factory's 1 for the slow branch (period >= 20), where it sets where the reads fall.
    // Opposite-polarity outputs need equal halves at every rate: the frame stores uniform timestamps.
    static constexpr uint16_t compare(uint16_t period, bool interleaved = false) {
        return interleaved || period <= 4 ? uint16_t(period / 2) : 1;
    }
    bool running() const { return running_; }
private:
    bool running_ = false;
};
}
