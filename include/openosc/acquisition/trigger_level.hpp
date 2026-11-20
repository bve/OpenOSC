#pragma once
#include "openosc/acquisition/frame.hpp"
#include <algorithm>
#include <cmath>

namespace openosc::scope {
// A held record keeps the trigger it was shown with, whatever is staged for the next capture.
class HeldTrigger {
public:
    void forget() { known_ = false; }
    // `edge`: what the index was found for (FrameMetadata::triggerEdge), kept with it.
    void remember(uint32_t sequence, bool found, uint32_t index, TriggerEdge edge = {}) {
        sequence_ = sequence; found_ = found; index_ = index; edge_ = edge; known_ = true;
    }
    bool recall(uint32_t sequence, bool &found, uint32_t &index, TriggerEdge *edge = nullptr) const {
        if (!known_ || sequence_ != sequence) return false;
        found = found_; index = index_;
        if (edge) *edge = edge_;
        return true;
    }

private:
    uint32_t sequence_ = 0, index_ = 0;
    TriggerEdge edge_;
    bool known_ = false, found_ = false;
};
class TriggerLevel {
public:
    struct Bounds {
        int16_t low, high;
        constexpr Bounds(int16_t minimum = -128, int16_t maximum = 127) : low(minimum), high(maximum) {}
        int16_t clamp(int code) const { return int16_t(std::clamp(code, int(low), int(high))); }
        int16_t nearest(double code) const { return int16_t(std::lround(std::clamp(code, double(low), double(high)))); }
        static Bounds between(double a, double b) {
            if (!std::isfinite(a) || !std::isfinite(b)) return {};
            const double lo = std::clamp(std::min(a, b), -128.0, 127.0);
            const double hi = std::clamp(std::max(a, b), -128.0, 127.0);
            const auto first = int16_t(std::ceil(lo)), last = int16_t(std::floor(hi));
            if (first <= last) return {first, last};
            const auto closest = int16_t(std::lround((lo + hi) * .5));
            return {closest, closest};
        }
    };
    // The level, set in the codes of the next capture, as a code of a record captured under other settings.
    static double inFrame(double level, const ChannelMetadata &settings, const ChannelMetadata &frame) {
        if (!settings.calibrated || !frame.calibrated || settings.voltsPerCode <= 0 || frame.voltsPerCode <= 0)
            return level;
        return (level - settings.zeroCode) * settings.voltsPerCode / frame.voltsPerCode + frame.zeroCode;
    }
};
// A change of the trigger channel's code scale (V/div, analog zero: ChannelScale) keeps the trigger at
// the same input voltage (GitHub #26): level' = (level - zero) * voltsPerCode / voltsPerCode' + zero'.
// Levels are whole codes of the comparator's -128..127: the rest, also what lies beyond that range, is
// carried as a remainder, so many steps add up to no drift and a round trip comes back to the voltage it
// left (a level held at the limit returns when the scale allows it again). Nothing changes until the
// change has been applied, and the remainder only belongs to the level, channel, zero and range it was
// computed for: any other change of the trigger or of its channel starts over, and a level set on
// purpose (forget) is the new anchor.
class TriggerFollow {
public:
    struct Scale { double zeroCode = 0, voltsPerCode = 0; };
    // Where the remainder belongs: the trigger's level and channel, its channel's zero and range.
    struct Place {
        int16_t level = 0, position = 0;
        uint8_t channel = 0, range = 0;
        bool operator==(const Place &o) const { return level == o.level && position == o.position && channel == o.channel && range == o.range; }
    };
    struct Proposal {
        int16_t level = 0;
        Place place;
        double remainder = 0;
        bool limited = false; // The voltage lies beyond the allowed codes: the level waits at the limit.
    };
    // `current`: the trigger now; `position`/`range`: its channel's next zero and range.
    Proposal propose(const Place &current, int position, unsigned range, const Scale &before, const Scale &after,
                     TriggerLevel::Bounds bounds = {}) const {
        const double carried = owned_ && place_ == current ? remainder_ : 0;
        const double exact = (current.level + carried - before.zeroCode) * before.voltsPerCode / after.voltsPerCode + after.zeroCode;
        // The voltage's nearest code, or the comparator's limit when that code is out of its reach
        // (rounded within one code past each end: far beyond it is the limit all the same).
        const auto next = bounds.nearest(exact);
        return {next, {next, int16_t(position), current.channel, uint8_t(range)}, exact - next, std::abs(exact - next) > .5};
    }
    void commit(const Proposal &proposal) { anchor(proposal.place, proposal.remainder, proposal.limited); }
    void anchor(const Place &place, double remainder, bool limited) {
        place_ = place; remainder_ = remainder; owned_ = true; limited_ = limited;
    }
    bool limited(const Place &place) const { return owned_ && place_ == place && limited_; }
    void observe(const Place &now) { if (owned_ && !(place_ == now)) owned_ = false; }
    // A level set on purpose (TRIG:LEV, the level keys, 50 %, AUTO) is the new anchor, also when it is
    // the same whole code: nothing of an earlier remainder is carried.
    void forget() { owned_ = false; }

private:
    double remainder_ = 0;
    Place place_;
    bool owned_ = false, limited_ = false;
};
} // namespace openosc::scope
