#pragma once
#include "openosc/acquisition/frame.hpp"
#include <cstddef>

namespace openosc::calibration {
// Loaded once before any capture. Frames can keep pointers to these coefficients through HOLD.
class InterleaveMatchProfiles {
public:
    bool load(uint16_t factoryCrc, const uint32_t uid[3]);
    bool parse(const char *text, size_t bytes, uint16_t factoryCrc, const uint32_t uid[3]);
    const scope::InterleaveMatch *find(uint32_t clockHz, const Rational &rate,
                                       const scope::ChannelMetadata &channel) const {
        for (unsigned i = 0; i < count_; ++i) {
            const auto &entry = entries_[i];
            if (entry.clockMhz * 1000000U == clockHz &&
                entry.rate.numerator == rate.numerator && entry.rate.denominator == rate.denominator &&
                entry.rangeMv == channel.millivoltsPerDivision && entry.coupling == channel.coupling &&
                entry.position == channel.analogPosition)
                return &entry.filter;
        }
        return nullptr;
    }
    bool empty() const { return count_ == 0; }

private:
    static constexpr unsigned MaxProfiles = 4;
    static constexpr size_t MaxBytes = 2048;
    struct Entry {
        Rational rate;
        uint16_t clockMhz, rangeMv;
        int16_t position;
        scope::Coupling coupling;
        scope::InterleaveMatch filter;
    };
    Entry entries_[MaxProfiles]{};
    unsigned count_ = 0;
};
} // namespace openosc::calibration
