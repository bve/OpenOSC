#include "openosc/acquisition/auto_setup.hpp"
#include <algorithm>
#include <cmath>

namespace openosc::scope {
bool AutoSetup::vertical(const FrameView &frame,SignalSummary (&summaries)[2],uint8_t &usable) {
    bool change=false;
    uint8_t clipped=0;
    for (unsigned c=0;c<2;++c) {
        if (!trial_.frontend.enabled[c]) continue;
        const auto &channel=frame.metadata().channels[c];
        auto &range=trial_.frontend.range[c];
        const auto s=summaries[c]=Analyzer::summarize(frame,Channel(c),false); // Extrema: the edges come later, on the band.
        if (!s.valid) continue;
        // A shorter trial follows the trigger source's period. A slower signal
        // on the other channel may show just one plateau, so keep its survey range.
        const bool adjust = c == channel_ || trial_.timebaseNs >= SurveyTimebaseNs;
        if (s.minimum<=-125 || s.maximum>=125) {
            if (adjust && range+1<VoltageRanges::Count) { ++range; change=true; }
            else clipped|=1U<<c;
            continue;
        }
        const double peak=std::max(std::abs(s.minimum-channel.zeroCode),std::abs(s.maximum-channel.zeroCode));
        const auto rangeFor=[](double peakMillivolts) { // The smallest range showing the peak within 3 divisions.
            unsigned next=0;
            while (next+1<VoltageRanges::Count && VoltageRanges::millivolts(next)<peakMillivolts/3) ++next;
            return next;
        };
        if (s.maximum-s.minimum<4 && peak<4) {
            // Below 4 codes the signal is under 4 codes' worth: go straight to the range that would
            // show that much within 3 divisions (10 V/div to 500 mV/div to 40 mV/div), not one step.
            if (adjust && range) { range=std::min(range-1,rangeFor(4*channel.voltsPerCode*1000)); change=true; }
            continue;
        }
        usable|=1U<<c;
        if (!adjust) continue;
        const double peakMillivolts=peak*channel.voltsPerCode*1000;
        const double divisions=peakMillivolts/VoltageRanges::millivolts(range);
        // Hysteresis avoids alternating ranges because of one noisy ADC code.
        if (divisions>=1.8 && divisions<=3.5) continue;
        const unsigned next=rangeFor(peakMillivolts);
        if (next!=range) { range=next; change=true; }
    }
    if (!change && clipped && !usable) state_=State::Clipped;
    return change;
}
} // namespace openosc::scope
