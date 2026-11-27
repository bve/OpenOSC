#include "openosc/measurement/dmm/hold.hpp"
#include "openosc/measurement/dmm/function.hpp"
#include "openosc/measurement/dmm/resolution.hpp"
#include "openosc/measurement/dmm/session.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace openosc::dmm {
bool HoldDetector::Signature::sameRange(const Signature &b) const {
    return sameMeasurement(b) && decimals == b.decimals && !std::strcmp(unit, b.unit);
}
bool HoldDetector::arm(uint64_t nowUs) {
    if (settings_.mode == HoldMode::Manual || !settings_.valid()) return false;
    cancel();
    phase_ = settings_.mode == HoldMode::Delayed ? Phase::Delay : Phase::Waiting;
    armedUs_ = lastUs_ = nowUs;
    return true;
}
void HoldDetector::cancel() {
    phase_ = Phase::Idle;
    captured_ = false;
    size_ = 0;
}
void HoldDetector::reselect() {
    size_ = 0;
    if (captured_) drop();
}
bool HoldDetector::sample(const Result &result, bool relative, Signature &signature, int64_t &counts, double &quantum) {
    const auto resolution = Resolution::of(result);
    quantum = resolution.quantum();
    const auto function = Functions::identify(result);
    if (function == Function::Unknown || !(quantum > 0) || std::strlen(result.unit) >= sizeof signature.unit) return false;
    const double value = std::round(result.primary.si / quantum);
    if (!(std::fabs(value) < 1e12)) return false;
    signature.function = unsigned(function); // Kind 5 and 7 are one function (Functions::identify).
    signature.current = result.currentInput;
    signature.relative = relative || result.peerRelative;
    signature.decimals = uint8_t(resolution.places());
    std::strcpy(signature.unit, result.unit);
    counts = int64_t(value);
    return true;
}
// The window restarts with a reading of another range or measurement, or after a gap; the ring keeps
// the newest Capacity readings.
void HoldDetector::push(const Signature &signature, int64_t counts, uint64_t timeUs) {
    if (size_ && (!windowSignature_.sameRange(signature) || timeUs - ring_[head_].timeUs > StaleUs)) size_ = 0;
    windowSignature_ = signature;
    head_ = (head_ + 1) % Capacity;
    ring_[head_] = {timeUs, counts};
    if (size_ < Capacity) ++size_;
}
// The newest readings back to the first one that is at least the stable time older than the newest
// with at least the minimum readings in between: all within the band, the newest no further than half the band from that first one (a
// drift spans the band, a plateau does not). Earlier readings are outside the window.
bool HoldDetector::stable() const {
    if (!size_) return false;
    const Point &last = ring_[head_];
    const uint64_t span = uint64_t(settings_.stableMs) * 1000;
    int64_t low = last.counts, high = last.counts;
    for (unsigned n = 1; n <= size_; ++n) {
        const Point &p = ring_[(head_ + Capacity - (n - 1)) % Capacity];
        if (p.counts < low) low = p.counts;
        if (p.counts > high) high = p.counts;
        const int64_t band = settings_.bandDigits;
        if (high - low > band) return false;
        if (last.timeUs - p.timeUs >= span && n >= settings_.minReadings) {
            const int64_t drift = last.counts > p.counts ? last.counts - p.counts : p.counts - last.counts;
            return 2 * drift <= band;
        }
    }
    return false; // Not yet the stable time and the minimum readings.
}
// Auto: overload, no reading or a value further than the rearm threshold from the snapshot, in the
// coarser of the two digits (a range change alone, the same value on another scale, is no departure).
bool HoldDetector::departed(const Result &result, double quantum) const {
    if (!(quantum > 0)) return true;
    const double digit = quantum > snapshotQuantum_ ? quantum : snapshotQuantum_;
    return std::fabs(result.primary.si - snapshot_.primary.si) > (double(settings_.rearmDigits) + 0.5) * digit;
}
void HoldDetector::capture(const Result &result, const Signature &signature, double quantum) {
    snapshot_ = result;
    snapshotSignature_ = signature;
    snapshotQuantum_ = quantum;
    captured_ = true;
    ++captures_;
    phase_ = Phase::Held;
    size_ = 0;
}
void HoldDetector::drop() {
    captured_ = false;
    size_ = 0;
    phase_ = settings_.mode == HoldMode::Auto ? Phase::Waiting : Phase::Idle;
}
void HoldDetector::observe(const Result &result, const Context &context) {
    if (phase_ == Phase::Idle || result.sequence <= lastSequence_) return;
    lastSequence_ = result.sequence;
    // A frozen or pending source: no reading of the measurement. The window breaks (its readings and
    // time before the pause do not count after it); a snapshot stays.
    if (result.packetHold || context.sourceHeld || context.awaiting) { size_ = 0; return; }
    Signature signature;
    int64_t counts = 0;
    double quantum = 0;
    const bool valid = result.primary.valid() && sample(result, context.relative, signature, counts, quantum);
    // Overload, no reading ("--.", blanks, dashes), an unparsed reading: no window; in Auto also a departure.
    if (!valid) {
        size_ = 0;
        if (phase_ == Phase::Held && settings_.mode == HoldMode::Auto) phase_ = Phase::Waiting;
        return;
    }
    // Another function, jack or REL: the snapshot is of something else now.
    if (captured_ && !snapshotSignature_.sameMeasurement(signature)) drop();
    if (phase_ == Phase::Idle) return;
    const uint64_t timeUs = result.timestampUs;
    // Two packets of one poll share its time: the second adds no time to a window (skipped), but a
    // real change of value in it is still a departure.
    const bool repeatedTime = timeUs <= lastUs_;
    if (!repeatedTime) lastUs_ = timeUs;
    const bool automatic = settings_.mode == HoldMode::Auto;
    const int64_t zero = settings_.zeroDigits;
    // Auto treats a reading inside the deadband as a disconnect, leaving the snapshot visible.
    // The DMM's own REL packets carry a difference, so zero there can be a connected source.
    if (automatic && !result.peerRelative && counts >= -zero && counts <= zero &&
        (result.kind == Kind::Dc || result.kind == Kind::Ac || result.kind == Kind::Capacitance)) {
        phase_ = Phase::Waiting;
        size_ = 0;
        return;
    }
    switch (phase_) {
    case Phase::Delay:
        // The first valid reading at or after the countdown's end (Delayed checks no stability).
        if (!repeatedTime && timeUs - armedUs_ >= uint64_t(settings_.delayMs) * 1000) capture(result, signature, quantum);
        break;
    case Phase::Waiting:
        if (repeatedTime) break;
        push(signature, counts, timeUs);
        if (stable()) capture(result, signature, quantum);
        break;
    case Phase::Held:
        // Delayed, Stable: one snapshot until HOLD or an explicit change. Auto: left by more than the
        // rearm threshold, a new plateau is sought from here; the snapshot stays shown meanwhile.
        if (automatic && departed(result, quantum)) {
            phase_ = Phase::Waiting;
            size_ = 0;
            if (!repeatedTime) push(signature, counts, timeUs);
        }
        break;
    case Phase::Idle: break;
    }
}
void HoldDetector::service(uint64_t nowUs) {
    if (size_ && nowUs > lastUs_ && nowUs - lastUs_ > StaleUs) size_ = 0; // The source went quiet.
}
uint32_t HoldDetector::remainingMs(uint64_t nowUs) const {
    if (phase_ != Phase::Delay || nowUs < armedUs_) return phase_ == Phase::Delay ? settings_.delayMs : 0;
    const uint64_t elapsed = (nowUs - armedUs_) / 1000;
    return elapsed >= settings_.delayMs ? 0 : uint32_t(settings_.delayMs - elapsed);
}
void HoldDetector::indicator(uint64_t nowUs, char *output, size_t capacity) const {
    if (!output || !capacity) return;
    const bool automatic = settings_.mode == HoldMode::Auto;
    switch (phase_) {
    case Phase::Idle: output[0] = 0; break;
    case Phase::Delay: std::snprintf(output, capacity, "DELAY %lu", (unsigned long)((remainingMs(nowUs) + 999) / 1000)); break;
    case Phase::Waiting: std::snprintf(output, capacity, "%s", automatic ? (captured_ ? "A-HOLD" : "A-WAIT") : "WAIT"); break;
    case Phase::Held: std::snprintf(output, capacity, "%s", automatic ? "A-HOLD" : "HOLD"); break;
    }
}
} // namespace openosc::dmm
