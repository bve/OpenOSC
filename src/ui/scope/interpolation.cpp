#include "openosc/ui/scope/source.hpp"
#include <algorithm>
#include <cmath>

namespace openosc::plot {
namespace {
// 65 fractional phases, including the next exact sample. Each eight-tap
// Lanczos kernel sums to one, so a constant input remains constant.
struct SincTable {
    int32_t weights[65][8]{};
    SincTable() {
        constexpr double pi=3.14159265358979323846;
        for (unsigned phase=0; phase<=64; ++phase) {
            double taps[8],sum=0;
            for (unsigned i=0; i<8; ++i) {
                const double x=int(i)-3-phase/64.;
                taps[i]=std::abs(x)<1e-12 ? 1 : std::sin(pi*x)*std::sin(pi*x/4)/(pi*pi*x*x/4);
                sum+=taps[i];
            }
            int32_t total=0;
            for (unsigned i=0; i<8; ++i) {
                weights[phase][i]=std::lround(taps[i]/sum*65536);
                total+=weights[phase][i];
            }
            weights[phase][3]+=65536-total;
        }
    }
};
}
bool Source::interpolate(int64_t position,int32_t (&values)[3]) {
    if (!valid_ || !count_ || position<0 || uint64_t(position)>(uint64_t(count_-1)<<16)) return false;
    const uint32_t first=uint64_t(position)>>16, fraction=position&65535;
    if (!sample(first,values)) return false;
    if (!fraction) return true;
    if (options_.interpolation==Interpolation::Sinc && first>=3 && first+4<count_) {
        static const SincTable table;
        const auto *weights=table.weights[(fraction+512)>>10];
        int64_t sums[3]{};
        for (unsigned i=0; i<8; ++i) {
            int32_t sampleValues[3];
            if (!sample(first-3+i,sampleValues)) return false;
            for (unsigned c=0; c<3; ++c) sums[c]+=int64_t(sampleValues[c])*weights[i];
        }
        for (unsigned c=0; c<3; ++c) {
            const int32_t limit = c == MathView::TraceIndex ? MathLimitQ8 : 16777216;
            values[c] = std::clamp<int64_t>(sums[c] / 65536, -limit, limit);
        }
        return true;
    }
    int32_t next[3];
    if (!sample(first+1,next)) return false;
    if (options_.interpolation==Interpolation::Spline && first && first+2<count_) {
        int32_t before[3],after[3];
        if (!sample(first-1,before) || !sample(first+2,after)) return false;
        const double t=fraction/65536.,t2=t*t,t3=t2*t;
        for (unsigned c=0; c<3; ++c) {
            const double result=.5*((-t+2*t2-t3)*before[c]+(2-5*t2+3*t3)*values[c]+
                (t+4*t2-3*t3)*next[c]+(-t2+t3)*after[c]);
            const double limit = c == MathView::TraceIndex ? MathLimitQ8 : 16777216;
            values[c] = std::lround(std::clamp(result, -limit, limit));
        }
    } else for (unsigned c=0; c<3; ++c) values[c]+=int64_t(next[c]-values[c])*fraction/65536;
    return true;
}
} // namespace openosc::plot
