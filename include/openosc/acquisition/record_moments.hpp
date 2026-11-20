#pragma once
#include "openosc/acquisition/frame.hpp"
#include "openosc/dsp/sample_moments.hpp"

namespace openosc::scope {
// Count, sum, squares and extrema of one channel's codes [first, first+count), exactly as
// FrameView::signedCode reads them, ADC-bus correction included. The record is read in place,
// one contiguous run of a bank (or of a split lane's capture ring/history) at a time, through the
// DSP block kernels; a block holding a code the bus can corrupt is summed code by code and only
// those codes go through the frame's correction. For paired ADCs, the unsaturated offset is
// applied to the moments of the high byte. Records whose uint32 origin+index wraps are scalar.
class RecordMoments {
public:
    static bool measure(const FrameView &frame, Channel channel, uint32_t first, uint32_t count, dsp::Moments &out);
    static constexpr uint32_t Block = 512; // Codes per kernel call: a refused block costs little.
};
} // namespace openosc::scope
