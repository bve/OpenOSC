#include "openosc/acquisition/frame.hpp"

namespace openosc::scope {
FrameView FrameView::withInterleaveMatch(const InterleaveMatch *match) const {
    if (!valid_ || metadata_.layout != Layout::InterleavedCh1) return {};
    auto metadata = metadata_;
    metadata.interleaveMatch = match;
    return FrameView(metadata, banks_, wordsPerBank_, &split_);
}

FrameView FrameView::slice(uint32_t first,uint32_t count) const {
    if (!valid_ || !count || first>=metadata_.samples || count>metadata_.samples-first) return {};
    auto view=*this; auto &metadata=view.metadata_;
    const uint64_t ticks=uint64_t(first)*metadata.samplesPerSecond.denominator;
    const uint64_t seconds=ticks/metadata.samplesPerSecond.numerator;
    const uint64_t remainder=ticks%metadata.samplesPerSecond.numerator;
    const uint64_t fraction=remainder*1000000/metadata.samplesPerSecond.numerator;
    if (seconds>(UINT64_MAX-metadata.timestampUs)/1000000) return {};
    const uint64_t duration=seconds*1000000;
    if (fraction>UINT64_MAX-metadata.timestampUs-duration) return {};
    metadata.timestampUs+=duration+fraction;
    // Retain uint32 wrap-before-modulo for legacy origins near UINT32_MAX.
    const bool wraps=uint64_t(metadata.origin)+first+count-1>UINT32_MAX;
    metadata.origin=uint32_t(metadata.origin+first);
    if (!wraps || !(metadata.ring&(metadata.ring-1))) metadata.origin%=metadata.ring;
    metadata.samples=count;
    if (metadata.triggered && metadata.triggerIndex>=first && metadata.triggerIndex-first<count) metadata.triggerIndex-=first;
    else metadata.clearTrigger();
    // A shifted trigger keeps its pretrigger point only while that point stays in the slice.
    const int64_t anchor=int64_t(metadata.triggerIndex)-metadata.triggerShift;
    if (anchor<0 || anchor>=int64_t(count)) metadata.triggerShift=0;
    // The configured pretrigger point too, while it stays in the slice.
    if (metadata.pretriggerKnown && metadata.pretrigger>=first && metadata.pretrigger-first<count) metadata.pretrigger-=first;
    else { metadata.pretrigger=0; metadata.pretriggerKnown=false; }
    view.valid_=view.validate();
    return view;
}
} // namespace openosc::scope
