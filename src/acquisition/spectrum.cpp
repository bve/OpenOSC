#include "openosc/acquisition/spectrum.hpp"
#include <algorithm>
#include <cmath>
#include "arm_math.h"

namespace openosc::scope {
namespace {
constexpr double Pi=3.14159265358979323846;
constexpr unsigned CicOrder=4; // Plain averaging leaves alias lobes at only -13 dB.
double fourthPower(double v) { const double squared=v*v; return squared*squared; }
// The CIC correction visits contiguous bins. Generate sin(k * angle) using the
// recurrence in double precision; the amplitude array ultimately stores floats.
class BinSine {
public:
    explicit BinSine(double angle) : factor_(2*std::cos(angle)), current_(std::sin(angle)) {}
    double next() {
        const double value=current_;
        current_=factor_*current_-previous_;
        previous_=value;
        return value;
    }
private:
    double factor_, current_, previous_=0;
};
}
void Spectrum::window(SpectrumStorage &s,unsigned count,SpectrumWindow type) {
    if (s.windowCount==count && s.cachedWindow==type) return;
    s.windowCount=count; s.cachedWindow=type; s.weightSum=0;
    for (unsigned i=0;i<count;++i) {
        const double angle=2*Pi*i/(count-1);
        const float weight=type==SpectrumWindow::Rectangular ? 1 : type==SpectrumWindow::Hann ? .5-.5*std::cos(angle) :
            type==SpectrumWindow::Hamming ? .54-.46*std::cos(angle) : .42-.5*std::cos(angle)+.08*std::cos(2*angle);
        s.weights[i]=weight; s.weightSum+=weight;
    }
}
// CMSIS-DSP real FFT (v1.14.4, the algorithm family the factory firmware links): the packed
// output keeps DC in [0], Nyquist in [1], then re/im pairs of bins 1..count/2-1.
bool Spectrum::transform(SpectrumStorage &s,unsigned count) {
    arm_rfft_fast_instance_f32 instance;
    if (arm_rfft_fast_init_f32(&instance,uint16_t(count))!=ARM_MATH_SUCCESS) return false;
    arm_rfft_fast_f32(&instance,s.real,s.imaginary,0);
    return true;
}
bool Spectrum::calculate(SpectrumStorage &s,const FrameView &frame,const SpectrumOptions &options) {
    s.result={};
    if (!frame.valid() || !options.valid() || !frame.metadata().channels[options.channel].enabled) return false;
    unsigned count=SpectrumStorage::Capacity;
    while (count>frame.metadata().samples) count>>=1;
    if (count<SpectrumStorage::MinimumCount) return false;
    window(s,count,options.window);
    // Spanning the whole record gives the resolution its time window allows; the
    // CIC filter keeps content above the reduced Nyquist from folding back.
    const unsigned step=frame.metadata().samples/count;
    float sum=0;
    // Modular CIC: the integrators may wrap, the output may not. It reaches code*step^4: 2^39 at the
    // deepest decimation (524288/2048=256), so 64-bit registers (32 bits wrapped there before).
    uint64_t integrators[CicOrder]={},delays[CicOrder]={};
    const auto push=[&](int32_t sample) {
        integrators[0]+=uint64_t(int64_t(sample));
        for (unsigned n=1;n<CicOrder;++n) integrators[n]+=integrators[n-1];
    };
    const auto pull=[&]() {
        uint64_t value=integrators[CicOrder-1];
        for (unsigned n=0;n<CicOrder;++n) { const uint64_t input=value; value-=delays[n]; delays[n]=input; }
        return int64_t(value);
    };
    // The record in order through its sequential reader: the same corrected codes as signedCode.
    auto reader=frame.cursor(Channel(options.channel));
    if (step==1) {
        unsigned i=0;
        if (reader.run(count,[&](int16_t sample) { s.real[i++]=sample; sum+=sample; })!=count) return false;
    } else {
        int16_t first;
        if (!frame.signedCode(Channel(options.channel),0,first)) return false;
        // Settle on the first sample so the record starts without a filter transient.
        for (unsigned n=0;n<CicOrder;++n) {
            for (unsigned j=0;j<step;++j) push(first);
            pull();
        }
        const double gain=fourthPower(step);
        for (unsigned i=0;i<count;++i) {
            if (reader.run(step,push)!=step) return false;
            s.real[i]=float(double(pull())/gain);
            sum+=s.real[i];
        }
    }
    const auto &channel=frame.metadata().channels[options.channel];
    const float offset=options.removeDc ? sum/count : channel.calibrated ? channel.zeroCode : 0;
    for (unsigned i=0;i<count;++i) s.real[i]=(s.real[i]-offset)*s.weights[i];
    if (!transform(s,count)) return false;
    auto &r=s.result;
    // Near the reduced Nyquist the filter cannot separate aliases, so that band is not shown.
    r.count=count; r.decimation=step; r.bins=(step>1 ? count*3/8 : count/2)+1; r.binHz=frame.metadata().samplesPerSecond.value()/(count*step);
    // Magnitudes of bins 1..: count/2-1 complex pairs at most, inside the packed output.
    const float *packed=s.imaginary;
    s.amplitude[0]=std::abs(packed[0]);
    const unsigned pairs=std::min(r.bins,count/2)-1;
    arm_cmplx_mag_f32(packed+2,s.amplitude+1,pairs);
    if (r.bins>count/2) s.amplitude[count/2]=std::abs(packed[1]);
    if (step>1) {
        BinSine signal(Pi/count), reduced(Pi/(count*step));
        // Response of the decimation filter, used to undo the attenuation in its passband.
        const float factor=2/s.weightSum;
        for (unsigned k=1;k<r.bins;++k) {
            const double response=fourthPower(signal.next()/(step*reduced.next()));
            s.amplitude[k]=(s.amplitude[k]*factor)/float(response);
        }
        s.amplitude[0]*=1/s.weightSum;
    } else {
        for (unsigned k=0;k<r.bins;++k)
            s.amplitude[k]*=(k==0 || k==count/2 ? 1 : 2)/s.weightSum;
    }
    for (unsigned k=0;k<r.bins;++k) {
        r.maximum=std::max(r.maximum,s.amplitude[k]);
        if (k && s.amplitude[k]>r.peakCodes) { r.peakCodes=s.amplitude[k]; r.peakBin=k; }
    }
    r.hasPeak=r.peakCodes>1e-5;
    double bin=r.peakBin;
    if (r.hasPeak && r.peakBin>1 && r.peakBin+1<r.bins) {
        const double a=std::log(std::max(s.amplitude[r.peakBin-1],1e-12f));
        const double b=std::log(std::max(s.amplitude[r.peakBin],1e-12f));
        const double c=std::log(std::max(s.amplitude[r.peakBin+1],1e-12f));
        const double divisor=a-2*b+c;
        if (std::abs(divisor)>1e-12) bin+=std::clamp(.5*(a-c)/divisor,-.5,.5);
    }
    r.peakHz=bin*r.binHz; r.valid=true;
    return true;
}
} // namespace openosc::scope
