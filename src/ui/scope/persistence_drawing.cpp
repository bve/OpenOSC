#include "openosc/ui/scope/painter.hpp"
#include <algorithm>
#include <array>

namespace openosc::plot {
namespace {
// One set of four bits per pixel in each band. The guarded nibble additions
// compare eight intensities at once, with no carry between pixels.
std::array<uint32_t,3> intensityMasks(uint32_t packed) {
    constexpr uint32_t HighBits=0x88888888U;
    const uint32_t low=packed&~HighBits;
    const uint32_t visible=((low+0x77777777U)|packed)&HighBits; // At least 1.
    const uint32_t medium=((low+0x22222222U)|packed)&HighBits;  // At least 6.
    const uint32_t bright=(low+0x55555555U)&packed&HighBits;    // At least 11.
    return {((visible&~medium)>>3)*15, ((medium&~bright)>>3)*15, (bright>>3)*15};
}
struct RowSpan {
    unsigned first=0,last=0;
    void flush(Path &path,unsigned y) {
        if (first==last) return;
        path.vertex(first,y); path.vertex(last,y); path.gap();
        first=last;
    }
    void add(Path &path,unsigned y,unsigned start,unsigned stop) {
        if (start!=last) { flush(path,y); first=start; }
        last=stop;
    }
    void addMask(Path &path,unsigned y,uint32_t mask,unsigned base,unsigned bitsShift,unsigned left,unsigned right) {
        while (mask) {
            const unsigned firstBit=__builtin_ctz(mask);
            const uint32_t shifted=mask>>firstBit;
            const unsigned length=shifted==UINT32_MAX ? 32 : __builtin_ctz(~shifted);
            const unsigned start=std::max(left,base+(firstBit>>bitsShift));
            const unsigned stop=std::min(right,base+((firstBit+length)>>bitsShift));
            if (start<stop) add(path,y,start,stop);
            mask &= mask + (uint32_t{1}<<firstBit); // Remove the first contiguous run of ones.
        }
    }
};
} // namespace
void Painter::persistence() {
    const auto &o=model_.options();
    const auto &bitmap=model_.storage().persistence;
    if (!bitmap.hasPixels()) return;
    // A redraw can be limited to a band or small dirty area. Scan only its part of the bitmap.
    const auto &clip=layer_->_clip_area;
    const unsigned left=clip.x1-area_.x1, right=clip.x2-area_.x1+1;
    const unsigned top=clip.y1-area_.y1, bottom=clip.y2-area_.y1+1;
    const bool infinite=bitmap.infinite();
    const unsigned wordPixels=infinite ? Persistence::InfinitePixelsPerWord : Persistence::FinitePixelsPerWord;
    const unsigned firstWord=left/wordPixels, endWord=(right+wordPixels-1)/wordPixels;
    // Back to front as the traces: the other channel, the selected one, math (X-Y: one trail).
    const unsigned front=model_.frontChannel(), order[3]={front^1,front,2};
    for (unsigned c : order) {
        if (o.mode==Mode::XY && c!=0) continue;
        if (c<2 && !o.channels[c].visible) continue;
        if (c==2 && (o.math==Math::Off || !infinite)) continue;
        const auto color=c<2 ? palette_.channels[c] : palette_.math;
        const unsigned opacity=c<2 ? o.channels[c].opacity : 255;
        if (!opacity) continue;
        Path trails[] = {
            {layer_,area_,color,uint8_t(opacity*40/255),1,model_.statistics()},
            {layer_,area_,color,uint8_t(opacity*80/255),1,model_.statistics()},
            {layer_,area_,color,uint8_t(opacity*128/255),1,model_.statistics()}
        };
        for (unsigned y=top; y<bottom; ++y) {
            const auto *row=bitmap.packedRow(c,y);
            if (!row) continue;
            RowSpan spans[3];
            for (unsigned i=firstWord; i<endWord; ++i) {
                const uint32_t packed=row[i];
                if (!packed) continue;
                const unsigned base=i*wordPixels;
                if (infinite || packed==UINT32_MAX) {
                    spans[2].addMask(trails[2],y,packed,base,infinite ? 0 : 2,left,right);
                } else {
                    const auto masks=intensityMasks(packed);
                    for (unsigned band=0; band<3; ++band)
                        spans[band].addMask(trails[band],y,masks[band],base,2,left,right);
                }
            }
            for (unsigned band=0; band<3; ++band) spans[band].flush(trails[band],y);
        }
    }
}
} // namespace openosc::plot
