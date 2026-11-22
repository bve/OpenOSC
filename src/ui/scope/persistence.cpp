#include "openosc/ui/scope/persistence.hpp"
#include "openosc/ui/scope/model.hpp"
#include "raster_line.hpp"
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
constexpr uint32_t LowNibbles=0x0f0f0f0fU, Lanes=0x01010101U;
uint32_t fadeWord(uint32_t word,uint32_t ticks) {
    return subtractLanes(word&LowNibbles,ticks) | (subtractLanes((word>>4)&LowNibbles,ticks)<<4);
}
// A record's step of the density levels a run of rows crosses: in each crossed nibble (bit 0 set in
// `columns`) 0 -> FirstLevel, then n -> n + 1 below the gate (Persistence::step), eight at once.
// Each byte lane holds one nibble: below + zero <= 2 and n < gate <= 15, so nothing carries.
class Step {
public:
    Step(uint32_t columns,unsigned gate)
        : crossed_(columns*3), gates_(gate*Lanes), commonGate_(gate<=8 ? gate*0x11111111U : 0) {}
    uint32_t operator()(uint32_t word) const {
        if (commonGate_) {
            // A gate of at most eight can use bit 3 as a compare guard in every nibble.
            // The original bit 3 rules out levels already at or above eight.
            constexpr uint32_t Guard=0x88888888U;
            const uint32_t below=(~(word|(((word&~Guard)|Guard)-commonGate_))&Guard)>>3;
            const uint32_t pairs=word|(word>>1);
            const uint32_t zero=~(pairs|(pairs>>2))&0x11111111U;
            return word+((below+zero)&crossed_);
        }
        return lanes(word&LowNibbles,crossed_&LowNibbles) |
               (lanes((word>>4)&LowNibbles,(crossed_>>4)&LowNibbles)<<4);
    }
private:
    uint32_t lanes(uint32_t n,uint32_t crossed) const {
#if defined(__ARM_FEATURE_DSP)
        // gate - n saturates to 0 where n >= gate; + 254 then saturates to 255 exactly where it is not 0.
        const uint32_t below=__uqadd8(__uqsub8(gates_,n),0xfefefefeU)-0xfefefefeU;
        const uint32_t zero=__uqsub8(Lanes,n);
#else
        // Bit 7 of (n | 128) - gate stays set exactly where n >= gate; bit 7 of n + 127 where n > 0.
        const uint32_t below=(~((n|0x80808080U)-gates_)&0x80808080U)>>7;
        const uint32_t zero=(~(n+0x7f7f7f7fU)&0x80808080U)>>7;
#endif
        return n+((below+zero)&crossed);
    }
    uint32_t crossed_, gates_, commonGate_;
};
// Whether any of a word's eight nibbles equals `level`: a zero nibble of the difference borrows.
bool hasLevel(uint32_t word,unsigned level) {
    const uint32_t x=word^(level*0x11111111U);
    return (x-0x11111111U)&~x&0x88888888U;
}
// Every level above FirstLevel down by one: bit 7 of n + 125 is set exactly where n >= 3.
uint32_t halveLanes(uint32_t n) { return n-(((n+0x7d7d7d7dU)&0x80808080U)>>7); }
static_assert(Persistence::FirstLevel==2, "halveLanes keeps levels 1 and 2");
uint32_t halveWord(uint32_t word) { return halveLanes(word&LowNibbles) | (halveLanes((word>>4)&LowNibbles)<<4); }
} // namespace
void Persistence::clearPixels() {
    std::memset(words_,0,sizeof words_);
    std::memset(activeRows_,0,sizeof activeRows_);
    activeChannels_=0;
    peak_[0]=peak_[1]=0;
    densityClock_=false;
}
void Persistence::clearPlane(unsigned c) {
    if (c<2) peak_[c]=0;
    const auto bit=uint8_t(1U<<c);
    if (!(activeChannels_&bit)) return;
    std::memset(words_+rowOffset(c,0),0,MaximumHeight*FiniteStride*sizeof *words_);
    for (auto &row : activeRows_) row &= uint8_t(~bit);
    activeChannels_ &= uint8_t(~bit);
}
void Persistence::clear() { clearPixels(); duration_=0; }
void Persistence::grade(uint8_t channels) {
    channels &= 3;
    if (channels==graded_) return;
    clearPixels();
    graded_=channels;
}
bool Persistence::advance(uint32_t now, uint16_t duration) {
    if (duration_ != duration) {
        const bool changed=hasPixels();
        if (changed) clearPixels();
        duration_=duration;
        tickAt_=now;
        return changed;
    }
    // Only the trails fade with time; a density plane changes with its records alone.
    const auto trails=uint8_t(activeChannels_&~graded_);
    if (!trails) { tickAt_=now; return false; }
    if (!duration_ || infinite()) return false;
    // The UI loop may service the trail with a time sampled before its latest projection.
    const auto elapsed=int32_t(now-tickAt_);
    if (elapsed<=0) return false;
    const unsigned interval=(duration+13)/14;
    const unsigned ticks=unsigned(elapsed)/interval;
    if (!ticks) return false;
    tickAt_ += ticks*interval;
    if (ticks>=15) {
        if (!graded_) clearPixels();
        else for (unsigned c=0; c<2; ++c) if (trails&(1U<<c)) clearPlane(c);
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
            if (!(active&bit) || (graded_&bit)) continue;
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
    if (bits()) return row[x/InfinitePixelsPerWord] & (1U<<(x%InfinitePixelsPerWord)) ? 15 : 0;
    return (row[x/FinitePixelsPerWord]>>((x%FinitePixelsPerWord)*4))&15U;
}
void Persistence::mark(unsigned c,unsigned x,unsigned y) {
    auto *row=words_+rowOffset(c,y);
    if (bits()) row[x/InfinitePixelsPerWord] |= 1U<<(x%InfinitePixelsPerWord);
    else row[x/FinitePixelsPerWord] |= 15U<<((x%FinitePixelsPerWord)*4);
}
void Persistence::resize(unsigned width,unsigned height) {
    width=std::min(width,MaximumWidth); height=std::min(height,MaximumHeight);
    if (width==width_ && height==height_) return;
    width_=width; height_=height; clear();
}
void Persistence::line(unsigned c,int x1,int y1,int x2,int y2) {
    if (!duration_ || c>=(bits() ? 3U : 2U) || graded(c) || !width_ || !height_) return;
    if (!clipLine(x1,y1,x2,y2,width_-1,height_-1)) return;
    const auto bit=uint8_t(1U<<c);
    activeChannels_ |= bit;
    // A clipped segment visits every row between its endpoints.
    for (int y=std::min(y1,y2); y<=std::max(y1,y2); ++y) activeRows_[y] |= bit;
    rasterLine(x1,y1,x2,y2,[&](int x,int y) { mark(c,x,y); });
}
template <class Change> void Persistence::rewrite(unsigned c,Change change) {
    const auto bit=uint8_t(1U<<c);
    const unsigned rowWords=(width_+FinitePixelsPerWord-1)/FinitePixelsPerWord;
    bool any=false;
    auto *row=words_+rowOffset(c,0);
    for (unsigned y=0; y<height_; ++y, row+=FiniteStride) {
        if (!(activeRows_[y]&bit)) continue;
        uint32_t survivors=0;
        for (unsigned x=0; x<rowWords; ++x) {
            if (!row[x]) continue;
            row[x]=change(row[x]);
            survivors |= row[x];
        }
        if (survivors) any=true;
        else activeRows_[y] &= uint8_t(~bit);
    }
    if (!any) activeChannels_ &= uint8_t(~bit);
}
void Persistence::ageDensity(unsigned c,unsigned levels) {
    if (levels>=peak_[c]) { clearPlane(c); return; }
    const uint32_t lanes=levels*Lanes;
    rewrite(c,[lanes](uint32_t word) { return fadeWord(word,lanes); });
    peak_[c]=uint8_t(peak_[c]-levels); // Every level falls alike, the highest included.
}
void Persistence::halveDensity(unsigned c) {
    rewrite(c,halveWord);
    peak_[c]=uint8_t(peak_[c]-1); // Only ever above FirstLevel.
}
void Persistence::clearDensity() {
    for (unsigned c=0; c<2; ++c) if (graded(c)) clearPlane(c);
    densityClock_=false;
}
void Persistence::densityRecord(uint32_t now,uint16_t history) {
    if (!graded_) return;
    unsigned levels=0;
    if (history==InfinitePersistence) { // Never lowered with time.
    } else if (!densityClock_) {
        densityClock_=true;
        densityAt_=now;
    } else if (int32_t(now-densityAt_)>0) {
        const unsigned interval=std::max(1U,unsigned(history)/2);
        levels=std::min(unsigned(now-densityAt_)/interval,TopLevel);
        densityAt_ += unsigned(now-densityAt_)/interval*interval;
    }
    for (unsigned c=0; c<2; ++c) {
        if (!graded(c) || !hasPixels(c)) continue;
        if (levels) ageDensity(c,levels);
        if (peak_[c]>=TopLevel) halveDensity(c); // Saturated: room for the path every record crosses.
    }
    // The record's gate: FirstLevel + 1 plus the trailing zeros of a 32-bit xorshift (never 0).
    random_ ^= random_<<13; random_ ^= random_>>17; random_ ^= random_<<5;
    gate_=uint8_t(std::min<unsigned>(TopLevel,FirstLevel+1+unsigned(__builtin_ctz(random_))));
}
void Persistence::span(unsigned c,unsigned x,int y1,int y2) {
    // Beyond the plot by one row at most: still clipped, never Missing.
    const auto narrow=[](int y) { return int16_t(std::clamp(y,-1,int(MaximumHeight))); };
    const Range range{narrow(y1),narrow(y2)};
    spans(c,x,&range,1);
}
void Persistence::spans(unsigned c,unsigned x,const Range *ranges,unsigned count) {
    if (!graded(c) || x>=width_ || !height_) return;
    count=std::min(count,width_-x);
    while (count) {
        const unsigned n=std::min(count,FinitePixelsPerWord-x%FinitePixelsPerWord);
        group(c,x,ranges,n);
        x+=n; ranges+=n; count-=n;
    }
}
void Persistence::group(unsigned c,unsigned x,const Range *ranges,unsigned count) {
    // Two events a column, at the clipped span's first row (its lane) and at the row after its last
    // (8 plus its lane), the row in the upper bits: in this order, the crossed columns change only at events.
    uint16_t events[2*FinitePixelsPerWord];
    unsigned n=0, pixels=0, first=height_, end=0;
    const unsigned lane=x%FinitePixelsPerWord;
    for (unsigned i=0; i<count; ++i) {
        const auto range=ranges[i];
        if (range.y1==Missing) continue;
        const int top=std::max<int>(std::min(range.y1,range.y2),0);
        const int bottom=std::min<int>(std::max(range.y1,range.y2),int(height_)-1);
        if (top>bottom) continue; // Wholly above or below the plot.
        events[n++]=uint16_t(unsigned(top)<<4|(lane+i));
        events[n++]=uint16_t(unsigned(bottom+1)<<4|8U|(lane+i));
        pixels+=unsigned(bottom-top+1);
        first=std::min(first,unsigned(top)); end=std::max(end,unsigned(bottom+1));
    }
    if (!n) return;
    const auto bit=uint8_t(1U<<c);
    activeChannels_ |= bit;
    // The peak rises by one at most a record: to FirstLevel, else to peak + 1 where the gate allows.
    const unsigned target=peak_[c]+1;
    const bool watch=peak_[c]>=FirstLevel && gate_>peak_[c];
    peak_[c]=uint8_t(std::max<unsigned>(peak_[c],FirstLevel));
    bool reached=false;
    auto *const column=words_+rowOffset(c,0)+x/FinitePixelsPerWord;
    // Few pixels a column (points, a sparse trace): each column alone costs less than ordering the events.
    if (pixels<end-first+3*n/2) {
        for (unsigned k=0; k<n; k+=2) {
            const unsigned shift=(events[k]&7U)*4, stop=events[k+1]>>4;
            unsigned y=events[k]>>4;
            for (auto *word=column+y*FiniteStride; y<stop; ++y, word+=FiniteStride) {
                const uint32_t before=*word;
                const unsigned previous=(before>>shift)&15U;
                if (previous>=gate_) continue; // Already present, so its row is active.
                const unsigned level=step(previous,gate_);
                *word=(before&~(15U<<shift))|(level<<shift);
                reached |= level==target;
                activeRows_[y] |= bit;
            }
        }
    } else {
        for (unsigned k=1; k<n; ++k) {
            const auto event=events[k];
            unsigned j=k;
            for (; j && events[j-1]>event; --j) events[j]=events[j-1];
            events[j]=event;
        }
        uint32_t crossed=0; // Bit 0 of each crossed column's nibble.
        unsigned y=events[0]>>4;
        for (unsigned k=0; k<n; ++k) {
            const unsigned at=events[k]>>4;
            if (at>y && crossed) {
                const Step rows(crossed,gate_);
                for (auto *word=column+y*FiniteStride; y<at; ++y, word+=FiniteStride) {
                    const uint32_t before=*word, after=rows(before);
                    if (after==before) continue; // All crossed pixels were already active.
                    *word=after;
                    if (watch) reached |= hasLevel(after,target);
                    activeRows_[y] |= bit;
                }
            }
            y=at;
            const uint32_t lane=1U<<((events[k]&7U)*4);
            crossed=events[k]&8U ? crossed-lane : crossed+lane;
        }
    }
    if (watch && reached) peak_[c]=uint8_t(target);
}
void Model::accumulate(uint32_t now) {
    auto &p=storage_.persistence;
    if (!held_) p.densityRecord(now,options_.densityHistoryMs());
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
        if (c==2 && !p.bits()) continue; // Math keeps only an infinite trail, with no graded channel.
        const auto &trace=storage_.traces[c];
        if (p.graded(c)) {
            if (trace.enabled && !held_) p.record(c,trace,viewport_.width,options_.style); // HOLD: the levels stay.
            continue;
        }
        if (!trace.enabled || !options_.persistenceMs) continue;
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
namespace {
// Of a Vectors connector from (x - 1, from) to (x, to), the row of the source column farthest from
// `from`, as rasterLine splits it: the first (|to - from| + 1) / 2 rows (at least `from` itself)
// are the source column's, the rest the destination's.
int sourceEnd(int from,int to) {
    const int rows=std::abs(to-from), own=std::max(1,(rows+1)/2);
    return from+(to<from ? 1-own : own-1);
}
} // namespace
// A graded channel's record (GitHub #85): one vertical span a column, the pixels the trace draws
// there, so a record counts each pixel at most once. Vectors: the column's own parts of the
// connectors from the previous column's last point and to the next one's, and its extrema where
// the trace draws them (beyond both ends of its incoming connector); Envelope: the extrema; Points:
// the last point. Every part holds the column's last point, so their union is one span.
void Persistence::record(unsigned c,const Trace &trace,unsigned width,Style style) {
    width=std::min(width,MaximumWidth);
    constexpr unsigned Group=FinitePixelsPerWord;
    Range ranges[Group];
    for (unsigned x=0; x<width; ++x) {
        const auto &column=trace.columns[x];
        auto &range=ranges[x%Group];
        if (column.first==Missing) range={};
        else if (style==Style::Points) range={column.last,column.last};
        else {
            int low=std::min(column.a,column.b), high=std::max(column.a,column.b);
            if (style==Style::Vectors) {
                const int last=column.last;
                const bool before=x && trace.columns[x-1].first!=Missing;
                const int from=before ? trace.columns[x-1].last : column.first;
                const bool extrema=low<std::min(from,last) || high>std::max(from,last);
                if (!extrema) low=high=last;
                if (before) {
                    const int start=sourceEnd(from,last)+(last<from ? -1 : last>from ? 1 : 0);
                    low=std::min(low,start); high=std::max(high,start);
                }
                if (x+1<width && trace.columns[x+1].first!=Missing) {
                    const int end=sourceEnd(last,trace.columns[x+1].last);
                    low=std::min(low,end); high=std::max(high,end);
                }
            }
            range={int16_t(low),int16_t(high)};
        }
        if (x%Group==Group-1 || x+1==width) spans(c,x-x%Group,ranges,x%Group+1);
    }
}
} // namespace openosc::plot
