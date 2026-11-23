#pragma once
#include <cstdint>

namespace openosc::power {
struct BatteryState {
    uint8_t bars=0;
    bool valid=false,low=false,external=false;
    bool full=false; // Digital charge status HIGH while PC9 is HIGH, independent of the measured level.
    uint16_t millivolts=0; // The factory ADC scale with the instrument's trim (BatteryGauge::trim): an estimate.
    uint8_t percent=0;
    bool measured=false;
};

// PC4 through the factory's divider scale: an estimate, not a calibrated reading. Levels are for a
// 1S Li-ion cell under the instrument's own load. Two separate paths from each 2 s window average:
// - bars, volts and percent follow a slow filter (about 8 s), the bars with HysteresisMv;
// - the low verdict (LowBatteryPolicy's input) takes LowWindows consecutive raw windows under LowMv,
//   and clears after LowWindows consecutive ones at LowMv + HysteresisMv or more. A start-up sag of one
//   window, or the filter still settling from it, never makes the battery low.
// The instrument's display trim (Settings > Device > Power, GitHub #107) corrects the filtered value
// for bars, volts and percent: shown = factory * (1000 + gain) / 1000 + offset. Set it against an
// external meter to account for the PC4 divider and power path. The low verdict stays on the factory
// scale: no trim moves the automatic shutdown.
class BatteryGauge {
public:
    static constexpr uint16_t BarMv[4] = {3650, 3730, 3820, 3950}; // At or above: 2, 3, 4, 5 bars.
    static constexpr uint16_t LowMv = 3450;
    static constexpr uint16_t FullMv = 4200;
    static constexpr uint16_t HysteresisMv = 40;
    static constexpr uint16_t ShownStepMv = 20; // Display deadband, except at the full-charge boundary.
    static constexpr unsigned Window = 20;      // Samples per average: 2 s at the 100 ms poll.
    static constexpr uint8_t LowWindows = 3;   // 6 s of consecutive raw windows for a low verdict (and to clear it).
    static constexpr int TrimGainPermille = 100, TrimOffsetMv = 250; // The trim's bounds: +-10.0 %, +-250 mV.
    static constexpr bool trimValid(int gainPermille, int offsetMv) {
        return gainPermille >= -TrimGainPermille && gainPermille <= TrimGainPermille &&
               offsetMv >= -TrimOffsetMv && offsetMv <= TrimOffsetMv;
    }

    const BatteryState &state() const { return state_; }
    // A new trim shows at once on a measured level (no deadband, no new window); out of bounds: none.
    void trim(int gainPermille, int offsetMv) {
        if (!trimValid(gainPermille, offsetMv)) gainPermille = offsetMv = 0;
        if (gainPermille == gain_ && offsetMv == offset_) return;
        gain_ = int8_t(gainPermille); offset_ = int16_t(offsetMv);
        if (state_.measured) show(uint16_t((filtered_+8)/16), true);
    }
    void digital(bool external,bool chargeStatusHigh) {
        const uint8_t bits=uint8_t(external)|(uint8_t(chargeStatusHigh)<<1);
        if (bits!=candidate_) { candidate_=bits; stable_=1; }
        else if (stable_<11) ++stable_;
        if (stable_==11) {
            if (state_.external!=external) { sum_=0; averaged_=0; }
            state_.external=external; state_.full=external && chargeStatusHigh;
            if (external) state_.low=false;
            if (state_.external || !state_.measured) { lowRun_=clearRun_=0; low_=false; }
        }
    }
    void sample(uint16_t raw) {
        if (raw>4095) return;
        sum_+=raw;
        if (++averaged_<Window) return;
        const int32_t mv=int32_t(sum_*AdcScaleMv/AdcScaleCounts/Window);
        sum_=0; averaged_=0;
        filtered_=state_.measured ? filtered_+(mv*16-filtered_)/4 : mv*16; // 1/16 mV; about 8 s to settle.
        if (!state_.measured) { lowRun_=clearRun_=0; low_=false; }
        if (!state_.external) qualify(mv);
        show(uint16_t((filtered_+8)/16), !state_.valid || !state_.measured);
    }
    static uint8_t percent(uint32_t millivolts) {
        static constexpr struct { uint16_t mv; uint8_t pct; } curve[]={
            {3450,0},{3550,5},{3650,15},{3700,25},{3750,35},{3800,45},{3850,55},{3900,65},{3950,75},{4000,82},{4100,92},{FullMv,100}};
        if (millivolts<=curve[0].mv) return 0;
        for (unsigned i=1;i<sizeof curve/sizeof curve[0];++i)
            if (millivolts<curve[i].mv)
                return uint8_t(curve[i-1].pct+(millivolts-curve[i-1].mv)*(curve[i].pct-curve[i-1].pct)/(curve[i].mv-curve[i-1].mv));
        return 100;
    }
private:
    static constexpr uint32_t AdcScaleMv = 5000, AdcScaleCounts = 4166;
    // The nearest ADC code to 4.20 V converts back to 4199 mV on the factory scale.
    static constexpr uint32_t FullCode = (FullMv * AdcScaleCounts + AdcScaleMv / 2) / AdcScaleMv;
    static constexpr uint16_t FullCodeMv = FullCode * AdcScaleMv / AdcScaleCounts;
    static uint8_t level(int32_t mv) {
        uint8_t bars=1;
        for (auto threshold:BarMv) if (mv>=threshold) ++bars;
        return bars;
    }
    void qualify(int32_t raw) { // The raw window's verdict, independent of the filter.
        lowRun_=raw<LowMv ? uint8_t(lowRun_<LowWindows ? lowRun_+1 : LowWindows) : 0;
        clearRun_=raw>=LowMv+HysteresisMv ? uint8_t(clearRun_<LowWindows ? clearRun_+1 : LowWindows) : 0;
        if (lowRun_>=LowWindows) low_=true;
        else if (clearRun_>=LowWindows) low_=false;
    }
    uint16_t corrected(uint16_t factory) const {
        const int32_t mv = (int32_t(factory) * (1000 + gain_) + 500) / 1000 + offset_;
        return uint16_t(mv < 0 ? 0 : mv);
    }
    // `factory`: the filtered level on the factory scale; fresh: shown as it is (a first level, a new trim).
    void show(uint16_t factory, bool fresh) {
        if (factory == FullCodeMv) factory = FullMv;
        const uint16_t mv = corrected(factory);
        if (fresh) {
            state_.bars=level(mv);
            state_.millivolts=mv;
        } else {
            const uint8_t up=level(int32_t(mv)-HysteresisMv), down=level(int32_t(mv)+HysteresisMv);
            if (up>state_.bars) state_.bars=up;
            else if (down<state_.bars) state_.bars=down;
            const bool fullChanged = (mv >= FullMv) != (state_.millivolts >= FullMv);
            if (fullChanged || mv>=state_.millivolts+ShownStepMv || mv+ShownStepMv<=state_.millivolts) state_.millivolts=mv;
        }
        state_.low=low_;
        state_.percent=percent(state_.millivolts);
        state_.valid=state_.measured=true;
    }
    BatteryState state_;
    uint8_t candidate_=0,stable_=0,averaged_=0;
    int8_t gain_=0;
    int16_t offset_=0;
    uint32_t sum_=0;
    int32_t filtered_=0;
    uint8_t lowRun_=0,clearRun_=0;
    bool low_=false;
};

class LowBatteryPolicy {
public:
    struct State { bool warning=false,powerOff=false; };
    State poll(const BatteryState &battery,uint32_t now) {
        if (!battery.valid || !battery.low || battery.external) { active_=false; return {}; }
        if (!active_) { active_=true; began_=retryAt_=now; warned_=offSent_=false; }
        State result;
        if (!warned_ && uint32_t(now-began_)>=40000) { result.warning=true; warned_=true; }
        if (uint32_t(now-began_)>=60000 && (!offSent_ || uint32_t(now-retryAt_)>=5000)) {
            result.powerOff=true; offSent_=true; retryAt_=now;
        }
        return result;
    }
private:
    uint32_t began_=0,retryAt_=0;
    bool active_=false,warned_=false,offSent_=false;
};
} // namespace openosc::power
