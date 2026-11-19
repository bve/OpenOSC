#include "openosc/ui/scope/model.hpp"
#include <algorithm>
#include <cmath>

namespace openosc::plot {
bool Model::projectSpectrum(const scope::FrameView &frame, bool recalculate) {
    if (options_.mode == Mode::Spectrum) for (auto &trace:storage_.traces) trace.enabled=false;
    auto &trace = storage_.fft;
    trace.enabled = false;
    auto &s=spectrum_;
    if (recalculate) {
        spectrumDirty_ = false;
        ++statistics_.fftCalculations;
        if (!scope::Spectrum::calculate(s,frame,options_.spectrum)) return true;
        statistics_.sourceReads += s.result.count*s.result.decimation;
    }
    if (!s.result.valid) return true;
    binSpan_=double(s.result.bins-1)/options_.zoom;
    firstBin_=std::clamp<int32_t>(options_.panPpb,0,Options::PanFull)/double(Options::PanFull)*(s.result.bins-1-binSpan_);
    trace.enabled=true;
    for (unsigned x=0;x<viewport_.width;++x) {
        const double a=firstBin_+binSpan_*x/(viewport_.width-1);
        const double b=firstBin_+binSpan_*std::min<unsigned>(x+1,viewport_.width-1)/(viewport_.width-1);
        float magnitude=0;
        if (b-a<1) {
            const unsigned bin=unsigned(a),next=std::min(bin+1,s.result.bins-1);
            magnitude=s.amplitude[bin]+(s.amplitude[next]-s.amplitude[bin])*(a-bin);
        } else for (unsigned k=unsigned(a);k<=std::min(unsigned(b),s.result.bins-1);++k)
            magnitude=std::max(magnitude,s.amplitude[k]);
        const float normalized=options_.spectrum.scale==scope::SpectrumScale::Dbfs ?
            (std::clamp(20*std::log10(std::max(magnitude,1e-6f)/128),-80.0f,0.0f)+80)/80 :
            s.result.maximum>0 ? magnitude/s.result.maximum*.85f : 0;
        const int16_t y=int16_t(std::lround((viewport_.height-1)*(1-normalized)));
        trace.columns[x]={y,y,y,y};
    }
    statistics_.columns=viewport_.width;
    return true;
}
double Model::spectrumFrequency(unsigned permille) const {
    return (firstBin_+binSpan_*std::min(permille,1000U)/1000)*spectrum().binHz;
}
} // namespace openosc::plot
