#include "openosc/ui/scope/persistence.hpp"
#include "openosc/ui/scope/model.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#if defined(__ARM_FEATURE_DSP)
#include <arm_acle.h>
#endif

namespace openosc::plot {
namespace {
uint32_t subtractLanes(uint32_t nibbles,uint32_t ticks) {
#if defined(__ARM_FEATURE_DSP)
    return __uqsub8(nibbles,ticks);
#else
    // Each byte holds one nibble. The guard bit prevents borrows across bytes and
    // remains set exactly where the difference is non-negative.
    constexpr uint32_t Guard=0x80808080U;
    const uint32_t difference=(nibbles|Guard)-ticks;
    return difference&(((difference&Guard)>>7)*15);
#endif
}
uint32_t fadeWord(uint32_t word,uint32_t ticks) {
    constexpr uint32_t LowNibbles=0x0f0f0f0fU;
    return subtractLanes(word&LowNibbles,ticks) | (subtractLanes((word>>4)&LowNibbles,ticks)<<4);
}
} // namespace
void Persistence::clearPixels() {
    std::memset(words_,0,sizeof words_);
    std::memset(activeRows_,0,sizeof activeRows_);
    activeChannels_=0;
}
void Persistence::clear() { clearPixels(); duration_=0; }
bool Persistence::advance(uint32_t now, uint16_t duration) {
    if (duration_ != duration) {
        const bool changed=hasPixels();
        if (changed) clearPixels();
        duration_=duration;
        tickAt_=now;
        return changed;
    }
    if (!hasPixels()) { tickAt_=now; return false; }
    if (infinite()) return false;
    // The UI loop may service the trail with a time sampled before its latest projection.
    const auto elapsed=int32_t(now-tickAt_);
    if (elapsed<=0) return false;
    const unsigned interval=(duration+13)/14;
    const unsigned ticks=unsigned(elapsed)/interval;
    if (!ticks) return false;
    tickAt_ += ticks*interval;
    if (ticks>=15) {
        clearPixels();
        return true;
    }
    activeChannels_=0;
    const uint32_t ticksPerByte=ticks*0x01010101U;
    const unsigned rowWords=(width_+FinitePixelsPerWord-1)/FinitePixelsPerWord;
    for (unsigned y=0; y<height_; ++y) {
        unsigned active=activeRows_[y];
        if (!active) continue;
        for (unsigned c=0; c<2; ++c) {
            const unsigned bit=1U<<c;
            if (!(active&bit)) continue;
            auto *row=words_+(c*MaximumHeight+y)*FiniteStride;
            uint32_t survivors=0;
            for (unsigned x=0; x<rowWords; ++x) {
                if (!row[x]) continue;
                row[x]=fadeWord(row[x],ticksPerByte);
                survivors |= row[x];
            }
            if (!survivors) active &= ~bit;
        }
        activeRows_[y]=uint8_t(active);
        activeChannels_ |= uint8_t(active);
    }
    return true;
}
const uint32_t *Persistence::packedRow(unsigned c,unsigned y) const {
    if (c>=3 || y>=height_ || !(activeRows_[y]&(1U<<c))) return nullptr;
    return words_+rowOffset(c,y);
}
unsigned Persistence::pixel(unsigned c,unsigned x,unsigned y) const {
    const auto *row=x<width_ ? packedRow(c,y) : nullptr;
    if (!row) return 0;
    if (infinite()) return row[x/InfinitePixelsPerWord] & (1U<<(x%InfinitePixelsPerWord)) ? 15 : 0;
    return (row[x/FinitePixelsPerWord]>>((x%FinitePixelsPerWord)*4))&15U;
}
void Persistence::mark(unsigned c,unsigned x,unsigned y) {
    auto *row=words_+rowOffset(c,y);
    if (infinite()) row[x/InfinitePixelsPerWord] |= 1U<<(x%InfinitePixelsPerWord);
    else row[x/FinitePixelsPerWord] |= 15U<<((x%FinitePixelsPerWord)*4);
}
void Persistence::resize(unsigned width,unsigned height) {
    width=std::min(width,MaximumWidth); height=std::min(height,MaximumHeight);
    if (width==width_ && height==height_) return;
    width_=width; height_=height; clear();
}
void Persistence::line(unsigned c,int x1,int y1,int x2,int y2) {
    if (!duration_ || c>=(duration_==InfinitePersistence ? 3U : 2U) || !width_ || !height_) return;
    const int right=width_-1,bottom=height_-1;
    const auto code=[&](int x,int y) { return (x<0?1:x>right?2:0)|(y<0?4:y>bottom?8:0); };
    // Clip before Bresenham: heavily clipped traces never make an unbounded
    // off-screen walk and never wrap into another row/channel's bitmap.
    for (;;) {
        const int a=code(x1,y1),b=code(x2,y2);
        if (!(a|b)) break;
        if (a&b) return;
        const int out=a?a:b; int x,y;
        if (out&12) {
            y=out&4 ? 0 : bottom;
            x=x1+int64_t(x2-x1)*(y-y1)/(y2-y1);
        } else {
            x=out&1 ? 0 : right;
            y=y1+int64_t(y2-y1)*(x-x1)/(x2-x1);
        }
        if (a) { x1=x; y1=y; } else { x2=x; y2=y; }
    }
    const auto bit=uint8_t(1U<<c);
    activeChannels_ |= bit;
    // A clipped segment visits every row between its endpoints.
    for (int y=std::min(y1,y2); y<=std::max(y1,y2); ++y) activeRows_[y] |= bit;
    const int dx=std::abs(x2-x1),dy=-std::abs(y2-y1),sx=x1<x2?1:-1,sy=y1<y2?1:-1;
    int error=dx+dy;
    for (;;) {
        mark(c,x1,y1);
        if (x1==x2 && y1==y2) break;
        const int twice=2*error;
        if (twice>=dy) { error+=dy; x1+=sx; }
        if (twice<=dx) { error+=dx; y1+=sy; }
    }
}
void Model::accumulate() {
    auto &p=storage_.persistence;
    if (options_.mode==Mode::XY) {
        Point previous;
        for (unsigned i=0; i<xyCount_; ++i) {
            const auto point=storage_.xy[i];
            if (point.x==Missing || point.y==Missing) { previous={}; continue; }
            if (previous.x!=Missing && options_.style!=Style::Points) p.line(0,previous.x,previous.y,point.x,point.y);
            else p.line(0,point.x,point.y,point.x,point.y);
            previous=point;
        }
    } else for (unsigned c=0; c<3; ++c) {
        if (c==2 && options_.persistenceMs!=InfinitePersistence) continue;
        const auto &trace=storage_.traces[c];
        if (!trace.enabled) continue;
        int previous=Missing;
        for (unsigned x=0; x<viewport_.width; ++x) {
            const auto &column=trace.columns[x];
            if (column.first==Missing) { previous=Missing; continue; }
            if (options_.style==Style::Points) p.line(c,x,column.last,x,column.last);
            else {
                if (previous!=Missing) p.line(c,x-1,previous,x,column.first);
                p.line(c,x,std::min({column.first,column.a,column.b,column.last}),
                    x,std::max({column.first,column.a,column.b,column.last}));
            }
            previous=column.last;
        }
    }
}
} // namespace openosc::plot
