#pragma once
#include "openosc/ui/scope/options.hpp"

namespace openosc::plot {
class Persistence {
public:
    void clear();
    void resize(unsigned width,unsigned height);
    bool advance(uint32_t now, uint16_t duration); // True when the trail changed.
    void line(unsigned channel,int x1,int y1,int x2,int y2);
    unsigned pixel(unsigned channel,unsigned x,unsigned y) const; // Remaining intensity, 0..15.
    bool hasPixels() const { return activeChannels_!=0; }
    const uint32_t *packedRow(unsigned channel,unsigned y) const; // Null for empty rows. First pixel in the low bits.
    bool infinite() const { return duration_==InfinitePersistence; }
    static constexpr unsigned FinitePixelsPerWord=8, InfinitePixelsPerWord=32;
private:
    void mark(unsigned channel, unsigned x, unsigned y);
    void clearPixels();
    static constexpr unsigned FiniteStride=MaximumWidth/FinitePixelsPerWord, InfiniteStride=MaximumWidth/InfinitePixelsPerWord;
    static_assert(MaximumWidth%InfinitePixelsPerWord==0, "Packed rows must start on word boundaries");
    unsigned rowOffset(unsigned channel,unsigned y) const {
        return (channel*MaximumHeight+y)*(infinite() ? InfiniteStride : FiniteStride);
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
};
} // namespace openosc::plot
