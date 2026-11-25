#pragma once
#include "openosc/acquisition/frame.hpp"

namespace openosc::scope {
enum class SpectrumWindow : uint8_t { Rectangular,Hann,Hamming,Blackman };
enum class SpectrumScale : uint8_t { Linear,Dbfs };
struct SpectrumOptions {
    SpectrumWindow window=SpectrumWindow::Hann;
    SpectrumScale scale=SpectrumScale::Dbfs;
    uint8_t channel=0;
    bool removeDc=true;
    bool valid() const { return unsigned(window)<4 && unsigned(scale)<2 && channel<2; }
    bool same(const SpectrumOptions &b) const {
        return window==b.window && scale==b.scale && channel==b.channel && removeDc==b.removeDc;
    }
};
struct SpectrumResult {
    unsigned count=0,bins=0,peakBin=0,decimation=1;
    double binHz=0,peakHz=0;
    float peakCodes=0,maximum=0;
    bool valid=false,hasPeak=false;
};
struct SpectrumStorage {
    // Exactly 2048 points: the one CMSIS-DSP real FFT length whose tables are linked.
    static constexpr unsigned Capacity=2048, MinimumCount=Capacity;
    // real: the windowed input (the FFT works in place on it); imaginary: the packed FFT output.
    struct Work { float real[Capacity],imaginary[Capacity]; };
    // Work's raw bytes. They hold nothing between calculations, so the scope's software Glow
    // draws in them too: a spectrum is never computed during a draw (calculate() runs from
    // Model::project, outside the LVGL draw callbacks). Each use begins its own object here
    // with placement new and never reads what the other left.
    alignas(Work) unsigned char work[sizeof(Work)];
    float weights[Capacity],amplitude[Capacity/2+1];
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
    static bool transform(SpectrumStorage::Work &,unsigned);
};
} // namespace openosc::scope
