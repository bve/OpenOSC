#include "openosc/ui/scope/painter.hpp"
#include "openosc/ui/scope/format.hpp"
#include "openosc/ui/scope/display_scale.hpp"
#include "openosc/ui/components/elements.hpp"
#include <algorithm>
#include <cstdio>

namespace openosc::plot {
void Painter::spectrum() {
    const auto &result=model_.spectrum();
    const auto &options=model_.options().spectrum;
    const int width=model_.viewport().width,height=model_.viewport().height;
    if (model_.options().meter.axis == options.channel) return; // The DMM trace's hidden time axis: nothing of it.
    if (!result.valid) { text("FFT SOURCE UNAVAILABLE",4,18,palette_.readout,width-8,true); return; }
    const auto color=palette_.channel(options.channel);
    trace(model_.storage().fft,model_.options().mode==Mode::SpectrumOverlay ? palette_.math : color,255,1,Style::Vectors);
    if (width<160 || height<60) return;
    constexpr const char *windows[]={"Rect","Hann","Hamming","Blackman"};
    char label[64],value[32];
    std::snprintf(label,sizeof label,"FFT CH%u %s %s N%u",palette_.view.number(options.channel),DisplayScale::coupling(model_.metadata().channels[options.channel]),windows[unsigned(options.window)],result.count);
    // The top line: 0 Hz left, the header centred, the span's end right; the trace's lower edge stays clear.
    char low[24],high[24];
    Format::quantity(model_.spectrumFrequency(0),"Hz",low,sizeof low);
    Format::quantity(model_.spectrumFrequency(1000),"Hz",high,sizeof high);
    const int lw=ui::Elements::textWidth(low)+2,hw=ui::Elements::textWidth(high)+2,w=ui::Elements::textWidth(label)+2;
    const bool row=lw+w+hw+16<=width;
    text(label,row ? std::clamp((width-w)/2,lw+8,width-hw-8-w) : 4,2,color,w,true);
    text(low,4,row ? 2 : 17,palette_.readout,lw,true);
    text(high,width-hw-2,row ? 2 : 17,palette_.readout,hw,true);
    // Scale ends as bare numbers, see-through.
    const int scaleY=row ? 17 : 32;
    if (options.scale==scope::SpectrumScale::Dbfs) text("0",width-12,scaleY,palette_.readout,12,false);
    else {
        const auto &channel=model_.metadata().channels[options.channel];
        const double factor=channel.calibrated ? channel.voltsPerCode*channel.probeNumerator/channel.probeDenominator : 1;
        Format::quantity(result.maximum/.85*factor,channel.calibrated ? "Vpk" : "cnt",value,sizeof value,3);
        const int vw=ui::Elements::textWidth(value)+2;
        text(value,width-vw-2,scaleY,palette_.readout,vw,false);
    }
    if (options.scale==scope::SpectrumScale::Dbfs) text("-80",width-24,height-28,palette_.readout,24,false);
    if (model_.options().mode==Mode::Spectrum && (model_.options().cursors.time || model_.options().cursors.voltage)) cursors();
}
} // namespace openosc::plot
