#pragma once
#include <cstdint>

namespace openosc {
struct IdleSettings {
    // dimPercent: the backlight PWM while idle, 0 (off, as before) or 5..25 %, never above the
    // working brightness (dimmedBrightness).
    // powerOffMinutes: Off (0, default) or OffChoices; files before them may hold 1/10/120
    // (LegacyOff), read as they are and kept until the next change (nextPowerOff).
    uint32_t backlightSeconds=0,powerOffMinutes=0,dimPercent=0;
    static constexpr uint32_t DimLevels[]={0,5,10,15,20,25};
    static constexpr uint32_t OffChoices[]={0,15,30,60}, LegacyOff[]={1,10,120};
    static bool legacyOff(uint32_t minutes) { for (uint32_t m : LegacyOff) if (m==minutes) return true; return false; }
    // The next offered auto-off up (direction>0) or down, from any accepted value: a legacy value
    // moves to the nearest offered one in that direction (never to a shorter one going up), wrapping
    // through Off.
    static uint32_t nextPowerOff(uint32_t minutes,int direction) {
        constexpr unsigned n=sizeof OffChoices/sizeof *OffChoices;
        if (direction>0) { for (uint32_t m : OffChoices) if (m>minutes) return m; return OffChoices[0]; }
        for (unsigned i=n; i-- > 0;) if (OffChoices[i]<minutes) return OffChoices[i];
        return OffChoices[n-1];
    }
    bool valid() const {
        bool dim=false,off=legacyOff(powerOffMinutes);
        for (uint32_t level : DimLevels) dim=dim || dimPercent==level;
        for (uint32_t m : OffChoices) off=off || powerOffMinutes==m;
        return dim && off && (backlightSeconds==0 || backlightSeconds==30 || backlightSeconds==60 || backlightSeconds==120);
    }
    uint32_t dimmedBrightness(uint32_t brightness) const { return dimPercent<brightness ? dimPercent : brightness; }
    // Whether the key that ends the sleep only wakes (InputService::wake takes its stroke): only when
    // the screen it was pressed on is dark, the idle level in force being 0. A visible idle
    // backlight (5..25 %, or the working brightness below it) wakes and the key acts as usual.
    bool wakeTakesKey(bool dimmed,uint32_t brightness) const { return dimmed && dimmedBrightness(brightness)==0; }
};
class IdlePolicy {
public:
    struct State { bool dimmed,warning,powerOff; };
    void configure(const IdleSettings &settings,uint32_t now) { settings_=settings; activity(now); }
    void activity(uint32_t now) { activityAt_=offAt_=now; dimmed_=warned_=offSent_=false; }
    uint32_t quietMs(uint32_t now) const { return now-activityAt_; } // Since the last key.
    State poll(uint32_t now,bool inhibitPowerOff) {
        State state{};
        if (inhibitPowerOff) { offAt_=now; warned_=offSent_=false; }
        const uint32_t timeout=settings_.powerOffMinutes*60000;
        if (timeout && !inhibitPowerOff) {
            if (!warned_ && uint32_t(now-offAt_)>=timeout-20000) { warned_=true; state.warning=true; }
            if (uint32_t(now-offAt_)>=timeout && (!offSent_ || uint32_t(now-retryAt_)>=5000)) {
                state.powerOff=true; offSent_=true; retryAt_=now;
            }
        }
        if (settings_.backlightSeconds && uint32_t(now-activityAt_)>=settings_.backlightSeconds*1000) dimmed_=true;
        state.dimmed=dimmed_ && !warned_;
        return state;
    }
private:
    IdleSettings settings_;
    uint32_t activityAt_=0,offAt_=0,retryAt_=0;
    bool dimmed_=false,warned_=false,offSent_=false;
};
} // namespace openosc
