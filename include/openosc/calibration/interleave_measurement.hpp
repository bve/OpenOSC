#pragma once
#include "openosc/acquisition/frame.hpp"
#include <cstdint>

namespace openosc::calibration {
// The ZT703S interleave offset as the user measures it (Settings > Device > Scope calibration >
// Interleave offset): in the real opposed-clock interleave of the run mode that runs, on a quiet CH1
// (grounded or a steady DC), records captured WITHOUT the offset correction. The offset is
// mean(LOW, odd ring positions) - mean(HIGH, even) over Records records, so adding it to the HIGH
// points (InterleaveCorrection) aligns the two ADCs. Bounded: MaximumPoints of each record, one record
// a main-loop pass, a deadline. The input must be quiet: each ADC's spread about its own mean within
// SpreadLimit codes (a grounded CH1 on a ZT703S showed 4.7 codes rms on the HIGH ADC, 2.2 on the LOW;
// the limit stays clear of that noise). Gain and skew are not measured or corrected here.
class InterleaveOffsetMeasurement {
public:
    static constexpr unsigned Records = 4;
    static constexpr uint32_t MaximumPoints = 65536, TimeoutMs = 5000;
    static constexpr double SpreadLimit = 10.0;
    enum class Result : uint8_t { None, Running, Measured, NotQuiet, OutOfRange, Timeout, Interrupted, Cancelled, SaveFailed, Clipped };
    // A record whose points of either ADC sit at a rail (code -128 or 127) more than this share is an
    // overloaded input, not a measurement: an offset from clipped codes would be the rail's, not the ADC's.
    static constexpr double ClippedShare = 0.001;
    // A measurement at `coreHz` from the first record with sequence `first` on (the capture restarted
    // without correction).
    void begin(uint32_t now, uint32_t coreHz, uint32_t first);
    bool active() const { return result_ == Result::Running; }
    // An inspected record: true when it was taken (a fresh uncorrected interleaved one; its reader then
    // consumes it). Once Records are in, active() ends with Measured, NotQuiet, OutOfRange or Clipped.
    bool record(const scope::FrameView &frame);
    bool expired(uint32_t now) const { return active() && uint32_t(now - began_) >= TimeoutMs; }
    // Ends a running measurement with `result` (Timeout, Interrupted, Cancelled) or a measured one's
    // save failure (SaveFailed).
    void end(Result result) { result_ = result; }
    Result result() const { return result_; }
    uint32_t coreHz() const { return coreHz_; }
    float offset() const { return offset_; }
    double spread() const { return spread_; } // The larger of the two ADCs' rms spread, codes.
    unsigned records() const { return records_; }

private:
    void finish();
    double sum_[2]{}, squares_[2]{};
    uint64_t count_[2]{}, rails_[2]{};
    uint32_t began_ = 0, coreHz_ = 0, first_ = 0, last_ = 0;
    float offset_ = 0;
    double spread_ = 0;
    unsigned records_ = 0;
    bool seen_ = false;
    Result result_ = Result::None;
};
} // namespace openosc::calibration
