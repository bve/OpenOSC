#pragma once
#include "openosc/acquisition/analysis.hpp"
#include "openosc/acquisition/frontend.hpp"
#include "openosc/acquisition/record_depths.hpp"

namespace openosc {
struct ScopeRequest {
    scope::FrontendSettings frontend;
    scope::TriggerSettings trigger;
    uint64_t timebaseNs = 1000000;
    // Record memory: total 8-bit points of the enabled channels (RecordBudget), not per channel.
    uint32_t recordPoints = scope::RecordBudget::Default;
    uint32_t probeNumerator[2] = {1, 1}, probeDenominator[2] = {1, 1};
    bool rollAuto=true;
    bool longerRecord = true;
    // A qualified trigger needs a bounded record with the history around its candidate.
    bool rolling() const { return rollAuto && timebaseNs>=200000000 && trigger.mode==scope::TriggerMode::Auto && !trigger.qualified(); }
    unsigned channels() const { return scope::RecordBudget::channels(frontend.enabled[0], frontend.enabled[1]); }
    uint32_t depth() const { return scope::RecordBudget::perChannel(recordPoints, channels()); } // Points per channel.
    // At a fixed timebase, depth beyond the default budget can extend the captured time.
    uint32_t windowPoints() const {
        return longerRecord ? scope::RecordBudget::perChannel(scope::RecordBudget::Default, channels()) : 0;
    }
    // The one channel switch for every caller (panel, SCPI, settings): the record budget stays the
    // same total, the last channel on stays on, and the trigger moves to a channel that remains.
    bool setChannel(unsigned channel, bool on) {
        if (channel > 1 || (!on && frontend.enabled[channel] && !frontend.enabled[channel ^ 1])) return false;
        frontend.enabled[channel] = on;
        followChannels();
        return true;
    }
    void followChannels() {
        const unsigned source = unsigned(trigger.channel);
        if (source < 2 && !frontend.enabled[source] && frontend.enabled[source ^ 1]) trigger.channel = scope::Channel(source ^ 1);
    }
    bool same(const ScopeRequest &other) const {
        if (timebaseNs != other.timebaseNs || recordPoints != other.recordPoints || rollAuto!=other.rollAuto ||
            longerRecord != other.longerRecord ||
            trigger != other.trigger) return false;
        for (unsigned c = 0; c < 2; ++c)
            if (frontend.range[c] != other.frontend.range[c] || frontend.position[c] != other.frontend.position[c] ||
                frontend.coupling[c] != other.frontend.coupling[c] || frontend.enabled[c] != other.frontend.enabled[c] ||
                probeNumerator[c] != other.probeNumerator[c] || probeDenominator[c] != other.probeDenominator[c]) return false;
        return true;
    }
};
} // namespace openosc
