#pragma once
#include "openosc/ui/scope/model.hpp"

namespace openosc::plot {
class Source {
public:
    Source(const scope::FrameView &frame, const Options &options, Statistics &statistics);
    bool sample(uint32_t index, int32_t (&values)[3]);
    bool interpolate(int64_t positionQ16, int32_t (&values)[3]);
    bool enabled(unsigned channel) const { return channel < 3 && enabled_[channel]; }
    double mathDivisionQ8() const;
    static constexpr int32_t MathLimitQ8 = (1 << 30) - 1;

private:
    int32_t combine(int32_t first, int32_t second) const;
    uint8_t byteAt(uint32_t index, unsigned channel) const;
    uint8_t resolved(uint8_t byte, uint32_t index, unsigned channel) const;
    uint16_t word(uint32_t position) const { // FourBanks: lane position & 3, lane offset position >> 2.
        // (Not InterleavedCh1: its points come from FrameView::code.)
        return split_.historyWords ? split_.word(banks_, position & 3, position >> 2, ring_ >> 2) : banks_[position & 3][position >> 2];
    }
    const scope::FrameView &frame_; // InterleavedCh1 is read through it (its pairs and correction).
    const uint16_t *banks_[4]{};
    scope::SplitHistory split_{};
    uint32_t count_ = 0, origin_ = 0, ring_ = 0, mask_ = 0;
    scope::Layout layout_ = scope::Layout::Linear;
    bool valid_ = false, powerOfTwo_ = false;
    const Options &options_;
    Statistics &statistics_;
    bool enabled_[3]{}, read_[2]{};
    int64_t ratioQ24_ = 16777216;
    int32_t zero1Q8_ = 0, zero2Q8_ = 0;
};
} // namespace openosc::plot
