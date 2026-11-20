#include "openosc/measurement/dmm/session.hpp"
#include "openosc/measurement/dmm/function.hpp"
#include "openosc/data/decimal.hpp"
#include <cmath>
#include <limits>

namespace openosc::dmm {
namespace {
Result asTemperature(const Result &packet, const TemperatureSettings &settings, bool typeK) {
    Result result;
    result.sequence = packet.sequence;
    result.timestampUs = packet.timestampUs;
    result.packetHold = packet.packetHold;
    result.currentInput = packet.currentInput;
    result.peerRelative = !typeK && packet.peerRelative;
    result.primary = {0, Quantity::Temperature, DataStatus::NoReading};
    const char *unit = TypeK::unit(settings.fahrenheit);
    std::strcpy(result.unit, unit);
    std::strcpy(result.rawUnit, unit);
    std::strcpy(result.mainText, "--");
    const bool source = !typeK || (Functions::identify(packet) == Function::MillivoltsDc && !packet.peerRelative);
    if (!source) return result;
    result.primary.status = packet.primary.status;
    if (!packet.primary.valid()) return result;
    double celsius = packet.primary.si;
    if (typeK && !TypeK::convert(packet.primary.si, settings.junctionCelsius(), celsius)) {
        result.primary.status = DataStatus::Overload;
        return result;
    }
    result.decimals = 1;
    result.primary.si = celsius;
    const double shown = TypeK::display(celsius, unit, result.peerRelative);
    result.rawMagnitude = unsigned(std::lround(std::fabs(shown) * 10));
    Decimal::fixed(shown, result.decimals, result.mainText, sizeof result.mainText);
    return result;
}
}

void Session::resetTemperatureReading(uint64_t nowUs) {
    userHold_ = frozen_ = false;
    relative_.clear();
    reselect();
    expire(nowUs);
    shown_ = (typeK_ || latest_.primary.quantity == Quantity::Temperature)
                 ? asTemperature(Result{}, temperatureSettings_, true) : Result{};
    shown_.primary.status = DataStatus::NoData;
}
void Session::configureTemperature(TemperatureSettings settings, uint64_t nowUs) {
    if (!settings.valid()) return;
    const bool changed = settings.fahrenheit != temperatureSettings_.fahrenheit ||
                         (typeK_ && settings.junctionDeciC != temperatureSettings_.junctionDeciC);
    temperatureSettings_ = settings;
    if (typeK_ && !settings.configured()) typeK(false, nowUs);
    else if (changed && (typeK_ || shown_.primary.quantity == Quantity::Temperature)) resetTemperatureReading(nowUs);
}
void Session::typeK(bool enabled, uint64_t nowUs) {
    if (enabled == typeK_ || (enabled && !temperatureSettings_.configured())) return;
    typeK_ = enabled;
    resetTemperatureReading(nowUs);
}

void History::push(double value) {
    if (!std::isfinite(value))
        return;
    samples_[cursor_] = value;
    cursor_ = (cursor_ + 1) % Capacity;
    if (size_ < Capacity)
        ++size_;
}
bool History::value(size_t index, double &output) const {
    if (index >= size_)
        return false;
    output = samples_[(cursor_ + Capacity - size_ + index) % Capacity];
    return true;
}
void Session::reselect() {
    resetStatistics();
    settle_.clear();
    detector_.reselect(); // The old function's snapshot and window go with the selection.
    awaiting_ = false;
}
Function Session::awaited() const { return awaiting_ ? awaited_ : Function::Unknown; }
void Session::restart(Function function, uint64_t untilUs) {
    reselect();
    awaited_ = function;
    awaitUntilUs_ = untilUs;
    awaiting_ = true;
}
bool Session::accept(const uint8_t *packet, size_t bytes, uint64_t timestampUs) {
    Result candidate;
    if (!Decoder::decode(packet, bytes, candidate))
        return false; // A bad packet changes nothing.
    candidate.timestampUs = timestampUs;
    candidate.sequence = ++sequence_;
    rawBytes_ = uint8_t(bytes < sizeof raw_ ? bytes : sizeof raw_);
    std::memcpy(raw_, packet, rawBytes_);
    latest_ = candidate;
    // Keep the wire result for DMM command confirmation. The rest of the session works in the
    // displayed quantity, so HOLD, REL, statistics and history see temperature, not millivolts.
    if (typeK_ || candidate.primary.quantity == Quantity::Temperature)
        candidate = asTemperature(candidate, temperatureSettings_, typeK_);
    // The hold mode sees the packet in the context it settles (before HOLD's stop: a mode's cycle never
    // freezes with the display): a packet ending the wait for a selected function counts, a local REL
    // this packet's function change clears does not apply to it.
    const auto function = Functions::identify(candidate);
    const bool known = function != Function::Unknown;
    const bool functionChanged = known && function_ != ~0U && (unsigned(function) != function_ || candidate.currentInput != functionCurrent_);
    const bool settles = (known && function == awaited_) || timestampUs >= awaitUntilUs_;
    // After an overload in Ohm, two readings are not shown: nothing below sees them (display, hold
    // mode, statistics, trend). Under HOLD the settling waits, as the factory decodes nothing then.
    if (functionChanged) settle_.clear();
    if (!userHold_ && !candidate.packetHold && settle_.skip(candidate)) return true;
    shown_ = candidate;
    detector_.observe(candidate, {relative_.active() && !functionChanged, candidate.packetHold, awaiting_ && !settles});
    if (held()) {
        if (!frozen_) {
            held_ = candidate;
            frozen_ = true;
        }
        return true;
    }
    frozen_ = false;
    // The context of the accumulated readings, compared only here: under HOLD nothing accumulates,
    // so a reading of another function or REL state that arrives then restarts the period on
    // resume, not while the held display still shows the old one. Function: V, mV, A, mA/uA (DC
    // or AC each), resistance, continuity, diode (kind 5 and 7 alike), capacitance.
    const bool changed = functionChanged || (contextKnown_ && candidate.peerRelative != contextPeerRelative_);
    if (changed) { relative_.clear(); resetStatistics(); }
    if (known) { function_ = unsigned(function); functionCurrent_ = candidate.currentInput; }
    contextPeerRelative_ = candidate.peerRelative; contextKnown_ = true;
    // After a selection, the first reading of the selected function (or the selector's deadline) seeds.
    if (awaiting_ && settles) awaiting_ = false;
    // "--." (the factory's placeholder) and overload restart the running statistics; the trend only
    // skips them. Other readings without a number (NoReading, Unsupported) neither count nor reset.
    if (placeholder(candidate) || candidate.primary.status == DataStatus::Overload) { stats_.reset(); return true; }
    if (awaiting_ || !candidate.primary.valid()) return true; // Unparsed or non-finite: neither counted nor a reset.
    if (trendQuantity_ != candidate.primary.quantity) { history_.reset(); trendQuantity_ = candidate.primary.quantity; }
    if (stats_.push(candidate.primary))
        history_.push(candidate.primary.si);
    return true;
}
bool Session::hold(bool enabled) {
    if (enabled && !shown_.primary.valid())
        return false;
    if (enabled) detector_.cancel(); // One HOLD at a time: the manual one ends a mode's cycle.
    userHold_ = enabled;
    if (enabled && !frozen_) {
        held_ = shown_;
        frozen_ = true;
    }
    if (!held())
        frozen_ = false;
    return true;
}
DataStatus Session::status(uint64_t nowUs, uint64_t staleAfterUs) const {
    if (!sequence_)
        return DataStatus::NoData;
    if (nowUs < latest_.timestampUs || nowUs - latest_.timestampUs > staleAfterUs || latest_.timestampUs < freshFromUs_)
        return DataStatus::Stale;
    return displayed().primary.status;
}
bool Command::encode(CommandGroup group, uint8_t (&bytes)[2]) {
    static constexpr uint8_t codes[] = {0x50, 0x40, 0x80, 0x60, 0x90, 0x10, 0x22, 0x20, 0x30, 0x21};
    const unsigned index = static_cast<unsigned>(group);
    if (index >= sizeof codes)
        return false;
    bytes[0] = bytes[1] = codes[index];
    return true;
}
} // namespace openosc::dmm
