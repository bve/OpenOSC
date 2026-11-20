#include "openosc/measurement/dmm/function.hpp"
#include <cstring>

namespace openosc::dmm {
Function Functions::identify(const Result &r) {
    if (r.primary.quantity == Quantity::Temperature) return Function::Temperature;
    switch (r.kind) {
    case Kind::Resistance: return Function::Resistance;
    case Kind::Capacitance: return Function::Capacitance;
    case Kind::Diode:
    case Kind::DiodeWithBeeper: return Function::Diode; // The diode with the beeper flag: the same function.
    case Kind::Continuity: return Function::Continuity;
    case Kind::Dc:
    case Kind::Ac: {
        const bool ac = r.kind == Kind::Ac;
        if (r.primary.quantity == Quantity::Current) {
            const bool fine = !std::strcmp(r.unit, "mA") || !std::strcmp(r.unit, "uA");
            return fine ? (ac ? Function::MilliampsAc : Function::MilliampsDc) : (ac ? Function::CurrentAc : Function::CurrentDc);
        }
        if (r.primary.quantity == Quantity::Voltage) {
            const bool fine = !std::strcmp(r.unit, "mV") || !std::strcmp(r.unit, "uV");
            return fine ? (ac ? Function::MillivoltsAc : Function::MillivoltsDc) : (ac ? Function::VoltageAc : Function::VoltageDc);
        }
        return Function::Unknown;
    }
    default: return Function::Unknown;
    }
}
CommandGroup Functions::group(Function f) {
    if (f == Function::VoltageDc || f == Function::VoltageAc) return CommandGroup::Voltage;
    if (f >= Function::Resistance && f <= Function::Capacitance) return CommandGroup::ResistanceGroup;
    if (f == Function::CurrentDc || f == Function::CurrentAc) return CommandGroup::Current;
    if (f == Function::MillivoltsDc || f == Function::MillivoltsAc || f == Function::Temperature) return CommandGroup::Millivolts;
    return CommandGroup::Milliamps;
}
bool FunctionSelector::request(Function function, const Result &latest, uint32_t now, uint64_t minimumTimestampUs) {
    if (function >= Function::Temperature || (busy() && function != requested_)) return false;
    if (busy()) return true;
    requested_ = function; attempts_ = 0; sequence_ = latest.sequence; nextAt_ = now; deadline_ = now + DeadlineMs;
    minimumTimestampUs_ = minimumTimestampUs;
    status_ = latest.sequence && latest.timestampUs >= minimumTimestampUs_ && Functions::identify(latest) == function ? DataStatus::Ok : DataStatus::Busy;
    return true;
}
bool FunctionSelector::poll(const Result &latest, uint32_t now, CommandGroup &command) {
    if (!busy()) return false;
    if (int32_t(now - deadline_) >= 0) { status_ = DataStatus::Timeout; return false; }
    if (latest.timestampUs < minimumTimestampUs_) return false;
    const Function shown = latest.sequence ? Functions::identify(latest) : Function::Unknown;
    if (latest.sequence && shown == requested_) { status_ = DataStatus::Ok; return false; }
    if (!latest.sequence || int32_t(now - nextAt_) < 0) return false;
    // Another key only once the last one has shown (see KeySettleMs); otherwise wait for the deadline.
    if (attempts_ && (latest.sequence == sequence_ || shown == Function::Unknown || shown == origin_)) return false;
    if (attempts_ >= 6) { status_ = DataStatus::Timeout; return false; }
    prepared_ = shown;
    command = Functions::group(requested_); return true;
}
} // namespace openosc::dmm
