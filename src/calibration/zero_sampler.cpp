#include "openosc/calibration/zero_sampler.hpp"
#include <cmath>
#include <cstring>

namespace openosc::calibration {
ScopeRequest ZeroSampler::trial(const ScopeRequest &original,unsigned range,unsigned point) {
    auto request=original;
    request.timebaseNs=1000000; request.rollAuto=false; request.trend=false; // DataAccess takes these frames into its trial area.
    request.trigger={};
    for (unsigned c=0;c<2;++c) {
        request.frontend.enabled[c]=true; request.frontend.range[c]=range<ZeroProfile::Ranges ? range : ZeroProfile::Ranges-1;
        request.frontend.position[c]=ZeroProfile::Positions[point<ZeroProfile::Points ? point : ZeroProfile::Points-1];
        request.frontend.coupling[c]=scope::Coupling::Dc;
        request.probeNumerator[c]=request.probeDenominator[c]=1;
    }
    return request;
}
void ZeroSampler::reset(unsigned range,uint32_t total,uint32_t now) {
    range_=range; samples_=0; total_=total; hasSequence_=false; appliedAt_=now;
    std::memset(histogram_,0,sizeof histogram_);
    std::memset(halves_,0,sizeof halves_); halfCounts_[0]=halfCounts_[1]=0;
}
bool ZeroSampler::matches(const scope::FrameView &frame) const {
    const auto &metadata=frame.metadata();
    if (metadata.rolling || metadata.timebaseNs!=1000000 || metadata.samples<128 ||
        int32_t(uint32_t(metadata.timestampUs/1000)-appliedAt_)<int32_t(SettleMs)) return false;
    for (const auto &channel:metadata.channels)
        if (!channel.enabled || channel.coupling!=scope::Coupling::Dc ||
            channel.millivoltsPerDivision!=scope::VoltageRanges::millivolts(range_)) return false;
    return true;
}
ZeroSampler::Take ZeroSampler::take(const scope::FrameView &frame,uint16_t factoryCrc) {
    if (complete() || !frame.valid() || !matches(frame) || (hasSequence_ && sequence_==frame.metadata().sequence)) return Take::None;
    for (const auto &channel:frame.metadata().channels)
        if (!channel.calibrated || channel.calibrationCrc!=factoryCrc) return Take::Uncalibrated;
    for (unsigned c=0;c<2;++c) {
        const double expected=frame.metadata().channels[c].zeroCode;
        if (!std::isfinite(expected) || (samples_ && expected!=expected_[c])) return Take::Uncalibrated;
        expected_[c]=expected;
    }
    sequence_=frame.metadata().sequence; hasSequence_=true;
    const auto &metadata=frame.metadata();
    const uint32_t count=metadata.samples<total_-samples_ ? metadata.samples : total_-samples_;
    for (uint32_t i=0;i<count;++i) {
        const unsigned half=samples_+i<total_/2 ? 0 : 1;
        ++halfCounts_[half];
        for (unsigned c=0;c<2;++c) {
            int16_t code=0;
            if (!frame.signedCode(scope::Channel(c),i,code)) return Take::Uncalibrated;
            ++histogram_[c][uint8_t(int8_t(code))^0x80];
            halves_[c][half]+=code;
        }
    }
    samples_+=count;
    return Take::Taken;
}
ZeroSampler::Statistics ZeroSampler::statistics(unsigned channel) const {
    constexpr int Window=12;
    const auto &bins=histogram_[channel];
    int median=0;
    for (uint32_t seen=0;median<255;++median) { seen+=bins[median]; if (seen*2>=samples_) break; }
    Statistics out;
    uint32_t count=0;
    double sum=0,squares=0;
    out.minimum=127; out.maximum=-128;
    for (int bin=0;bin<256;++bin) {
        if (!bins[bin]) continue;
        if (std::abs(bin-median)>Window) { out.outliers+=bins[bin]; continue; }
        const int code=bin-128;
        count+=bins[bin]; sum+=double(bins[bin])*code; squares+=double(bins[bin])*code*code;
        if (code<out.minimum) out.minimum=code;
        if (code>out.maximum) out.maximum=code;
    }
    if (count) { out.mean=sum/count; out.variance=squares/count-out.mean*out.mean; }
    return out;
}
bool ZeroSampler::quality(double deviation) const {
    for (unsigned c=0;c<2;++c) {
        const auto s=statistics(c);
        if (s.outliers>samples_/OutlierDivisor || std::abs(s.mean-expected_[c])>deviation ||
            s.variance>9 || s.minimum<=-125 || s.maximum>=125) return false;
    }
    return true;
}
double ZeroSampler::drift(unsigned channel) const {
    if (!halfCounts_[0] || !halfCounts_[1]) return 0;
    return std::abs(halves_[channel][0]/halfCounts_[0]-halves_[channel][1]/halfCounts_[1]);
}
} // namespace openosc::calibration
