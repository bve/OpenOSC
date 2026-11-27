#pragma once
#include "openosc/acquisition/frame.hpp"

namespace openosc::scope {
enum class SpectrumWindow : uint8_t { Rectangular,Hann,Hamming,Blackman };
enum class SpectrumScale : uint8_t { Linear,Dbfs };
struct SpectrumOptions {
    SpectrumWindow window=SpectrumWindow::Hann;
    SpectrumScale scale=SpectrumScale::Dbfs;
    uint8_t channel=0;
    // Keep the option bytes aligned as before inside the plot settings.
    bool removeDc : 1;
    bool fullRate : 1;
    constexpr SpectrumOptions() : removeDc(true), fullRate(false) {}
    bool valid() const { return unsigned(window)<4 && unsigned(scale)<2 && channel<2; }
    bool same(const SpectrumOptions &b) const {
        return window==b.window && scale==b.scale && channel==b.channel && removeDc==b.removeDc && fullRate==b.fullRate;
    }
};
struct SpectrumResult {
    unsigned count=0,bins=0,peakBin=0,decimation=1;
    double binHz=0,peakHz=0;
    float peakCodes=0,maximum=0;
    bool valid=false,hasPeak=false;
    uint32_t first=0; // First analyzed point in the record's chronological order.
};
struct SpectrumStorage {
    // Exactly 2048 points: the one CMSIS-DSP real FFT length whose tables are linked.
    static constexpr unsigned Capacity=2048, MinimumCount=Capacity;
    // real: the windowed input (the FFT works in place on it); imaginary: the packed FFT output.
    float real[Capacity],imaginary[Capacity],weights[Capacity],amplitude[Capacity/2+1];
    SpectrumResult result;
    unsigned windowCount=0;
    SpectrumWindow cachedWindow=SpectrumWindow::Rectangular;
    float weightSum=0;
};
class Spectrum {
public:
    static bool calculate(SpectrumStorage &,const FrameView &,const SpectrumOptions &);
private:
    static void window(SpectrumStorage &,unsigned,SpectrumWindow);
    static bool transform(SpectrumStorage &,unsigned);
};
} // namespace openosc::scope
