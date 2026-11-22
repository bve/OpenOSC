#pragma once
#include "openosc/ui/scope/options.hpp"
#include <algorithm>

namespace openosc::plot {
// The trails of a finite or infinite persistence and, for a graded channel (GitHub #85), its density
// in the same plane: a 4-bit, log-scale count a pixel of the fresh records that crossed it.
class Persistence {
public:
    void clear();
    void resize(unsigned width,unsigned height);
    bool advance(uint32_t now, uint16_t duration); // True when a trail changed (never a density plane).
    void line(unsigned channel,int x1,int y1,int x2,int y2);
    unsigned pixel(unsigned channel,unsigned x,unsigned y) const; // Remaining intensity, 0..15.
    bool hasPixels() const { return activeChannels_!=0; }
    bool hasPixels(unsigned channel) const { return channel<3 && (activeChannels_&(1U<<channel)); }
    const uint32_t *packedRow(unsigned channel,unsigned y) const; // Null for empty rows. First pixel in the low bits.
    bool infinite() const { return duration_==InfinitePersistence; }
    // One bit a pixel (three planes): an infinite persistence with no graded channel. Otherwise four.
    bool bits() const { return infinite() && !graded_; }
    // The graded channels (bit 0 CH1, bit 1 CH2): a change clears every plane (the layout may change).
    void grade(uint8_t channels);
    bool graded(unsigned channel) const { return channel<2 && (graded_&(1U<<channel)); }
    // Density levels: 0 never crossed; a first crossing sets 2, and level n counts about 2^(n-2)
    // crossings. Each fresh record draws one gate (3..15, gate g with probability 2^-(g-2)): every
    // pixel it crosses below the gate steps up by one. So a path every record crosses climbs one
    // level each time its count doubles, and a path crossed one record in 64 stays six levels lower.
    // History (ms): each whole half of it between fresh records lowers every level by one (halves
    // every count): a single crossing stays half to all of the history. Infinite: never lowered with
    // time; a plane whose peak reaches 15 has every level above 2 lowered by one at the next record
    // (ratios kept, nothing once seen lost).
    void densityRecord(uint32_t now, uint16_t history); // A fresh live record, before its spans.
    void delayDensity(uint32_t ms) { densityAt_ += ms; } // Time in HOLD ages nothing.
    void clearDensity(); // Every graded plane: a new trigger definition.
    unsigned peak(unsigned channel) const { return channel<2 ? peak_[channel] : 0; } // A plane's highest level.
    unsigned gate() const { return gate_; } // The current record's.
    static unsigned step(unsigned level,unsigned gate) { return level<gate ? std::max(level+1,2U) : level; }
    // A graded channel's fresh record: a span a column, the pixels its trace draws in that style.
    void record(unsigned channel,const Trace &trace,unsigned width,Style style);
    // One column's vertical span, its ends in either order; Missing in y1 for a column with none.
    struct Range { int16_t y1=Missing, y2=Missing; };
    void span(unsigned channel,unsigned x,int y1,int y2); // Clipped to the plot; once a column a record.
    // Columns x to x+count-1, a range each: eight columns of a packed word step together, row by row.
    void spans(unsigned channel,unsigned x,const Range *ranges,unsigned count);
    static constexpr unsigned FinitePixelsPerWord=8, InfinitePixelsPerWord=32, FirstLevel=2, TopLevel=15;
private:
    void group(unsigned channel,unsigned x,const Range *ranges,unsigned count); // Within one packed word.
    void mark(unsigned channel, unsigned x, unsigned y);
    void clearPixels();
    void clearPlane(unsigned channel);
    void ageDensity(unsigned channel,unsigned levels); // Every level down by `levels`.
    void halveDensity(unsigned channel); // Every level above FirstLevel down by one.
    template <class Change> void rewrite(unsigned channel,Change change); // Each non-empty word of a plane.
    static constexpr unsigned FiniteStride=MaximumWidth/FinitePixelsPerWord, InfiniteStride=MaximumWidth/InfinitePixelsPerWord;
    static_assert(MaximumWidth%InfinitePixelsPerWord==0, "Packed rows must start on word boundaries");
    unsigned rowOffset(unsigned channel,unsigned y) const {
        return (channel*MaximumHeight+y)*(bits() ? InfiniteStride : FiniteStride);
    }
    // Four-bit expiry never shortens retention. Its upper bound is
    // 15*ceil(duration/14) ms. Infinite persistence uses one bit per pixel in the same storage.
    static_assert(3*InfiniteStride<=2*FiniteStride, "Three infinite channels must fit in the finite bitmap");
    uint32_t words_[2*MaximumHeight*FiniteStride]{};
    uint8_t activeRows_[MaximumHeight]{};
    unsigned width_=0,height_=0;
    uint32_t tickAt_=0;
    uint16_t duration_=0;
    uint8_t activeChannels_=0;
    uint8_t graded_=0;
    uint32_t densityAt_=0, random_=0x2545f491U;
    bool densityClock_=false; // densityAt_ set: a record since the planes were cleared.
    uint8_t gate_=FirstLevel+1, peak_[2]{};
};
} // namespace openosc::plot
