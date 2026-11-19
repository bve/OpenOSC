#pragma once
#include <cstdint>

namespace openosc::power {
struct BatteryState {
    uint8_t bars=0;
    bool valid=false,low=false,external=false,full=false;
    uint16_t millivolts=0; // On the factory ADC scale, so an estimate rather than a calibrated reading.
    uint8_t percent=0;
    bool measured=false;
};

// PC4 through the factory's divider scale: an estimate, not a calibrated reading. Levels are for a
// 1S Li-ion cell under the instrument's own load. Two separate paths from each 2 s window average:
// - bars, volts and percent follow a slow filter (about 8 s), the bars with HysteresisMv;
// - the low verdict (LowBatteryPolicy's input) takes LowWindows consecutive raw windows under LowMv,
//   and clears after LowWindows consecutive ones at LowMv + HysteresisMv or more. A start-up sag of one
//   window, or the filter still settling from it, never makes the battery low.
class BatteryGauge {
public:
    static constexpr uint16_t BarMv[4] = {3650, 3730, 3820, 3950}; // At or above: 2, 3, 4, 5 bars.
    static constexpr uint16_t LowMv = 3450;
    static constexpr uint16_t HysteresisMv = 40;
    static constexpr uint16_t ShownStepMv = 20; // The volts/percent readout moves by at least this.
    static constexpr unsigned Window = 20;      // Samples per average: 2 s at the 100 ms poll.
    static constexpr uint8_t LowWindows = 3;   // 6 s of consecutive raw windows for a low verdict (and to clear it).

    const BatteryState &state() const { return state_; }
    void digital(bool external,bool full) {
        const uint8_t bits=uint8_t(external)|(uint8_t(full)<<1);
        if (bits!=candidate_) { candidate_=bits; stable_=1; }
        else if (stable_<11) ++stable_;
        if (stable_==11) {
            if (state_.external!=external) { sum_=0; averaged_=0; }
            if (state_.external && !external) state_.valid=state_.measured=false; // Re-measured from scratch.
            state_.external=external; state_.full=external && full;
            if (external) { state_.low=false; state_.valid=true; state_.bars=full ? 5 : 0; state_.measured=false; }
            if (state_.external || !state_.measured) { lowRun_=clearRun_=0; low_=false; }
        }
    }
    void sample(uint16_t raw) {
        if (raw>4095 || state_.external) return; // On external power PC4 does not read the battery.
        sum_+=raw;
        if (++averaged_<Window) return;
        const int32_t mv=int32_t(sum_*5000/4166/Window);
        sum_=0; averaged_=0;
        filtered_=state_.measured ? filtered_+(mv*16-filtered_)/4 : mv*16; // 1/16 mV; about 8 s to settle.
        if (!state_.measured) { lowRun_=clearRun_=0; low_=false; }
        qualify(mv);
        update(uint16_t((filtered_+8)/16));
    }
    static uint8_t percent(uint32_t millivolts) {
        static constexpr struct { uint16_t mv; uint8_t pct; } curve[]={
            {3450,0},{3550,5},{3650,15},{3700,25},{3750,35},{3800,45},{3850,55},{3900,65},{3950,75},{4000,82},{4100,92},{4150,100}};
        if (millivolts<=curve[0].mv) return 0;
        for (unsigned i=1;i<sizeof curve/sizeof curve[0];++i)
            if (millivolts<curve[i].mv)
                return uint8_t(curve[i-1].pct+(millivolts-curve[i-1].mv)*(curve[i].pct-curve[i-1].pct)/(curve[i].mv-curve[i-1].mv));
        return 100;
    }
private:
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
    void update(uint16_t mv) {
        if (!state_.valid || !state_.measured) {
            state_.bars=level(mv);
            state_.millivolts=mv;
        } else {
            const uint8_t up=level(int32_t(mv)-HysteresisMv), down=level(int32_t(mv)+HysteresisMv);
            if (up>state_.bars) state_.bars=up;
            else if (down<state_.bars) state_.bars=down;
            if (mv>=state_.millivolts+ShownStepMv || mv+ShownStepMv<=state_.millivolts) state_.millivolts=mv;
        }
        state_.low=low_;
        state_.percent=percent(state_.millivolts);
        state_.valid=state_.measured=true;
    }
    BatteryState state_;
    uint8_t candidate_=0,stable_=0,averaged_=0;
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
