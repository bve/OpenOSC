#pragma once
#include "openosc/board/current.hpp"
#include "openosc/measurement/dmm/session.hpp"

namespace openosc::dmm {
enum class Function : unsigned { VoltageDc, VoltageAc, Resistance, Continuity, Diode, Capacitance,
    CurrentDc, CurrentAc, MillivoltsDc, MillivoltsAc, MilliampsDc, MilliampsAc, Temperature, Unknown };
class Functions {
public:
    static Function identify(const Result &result);
    static CommandGroup group(Function function);
    // The board whose V key has AC V.F.C. as its third position: the ZT706S (its 66000-count
    // controller, as Ranges picks its tables). The ZT703S's F1 is DC/AC only: no VFC there.
    static constexpr bool VfcModel = board::Current::Features.dmmCounts == board::DmmCounts::Counts66000;
    // AC V.F.C. is shown: V AC with the VFC flag (Result::vfc). Not a function of its own: the
    // factory selects, ranges and decodes it as V AC and only marks it.
    static bool vfc(const Result &result) { return result.vfc && identify(result) == Function::VoltageAc; }
    // The function has ranges the AUTO/RANGE key steps through (a Ranges table on both models): V, mV,
    // A, mA (DC and AC), resistance, capacitance. Diode, continuity, temperature and Unknown
    // have none.
    static constexpr bool ranged(Function f) {
        return f < Function::Temperature && f != Function::Continuity && f != Function::Diode;
    }
    // The quantity a ranged function's reading is in (its unit's type, any prefix): Unknown for the others.
    static constexpr Quantity quantity(Function f) {
        switch (f) {
        case Function::VoltageDc: case Function::VoltageAc: case Function::MillivoltsDc: case Function::MillivoltsAc: return Quantity::Voltage;
        case Function::CurrentDc: case Function::CurrentAc: case Function::MilliampsDc: case Function::MilliampsAc: return Quantity::Current;
        case Function::Resistance: return Quantity::Resistance;
        case Function::Capacitance: return Quantity::Capacitance;
        default: return Quantity::Unknown;
        }
    }
    // The quantity a function's readings are in: continuity reads in ohms, the diode in volts.
    static constexpr Quantity reading(Function f) {
        return f == Function::Continuity ? Quantity::Resistance : f == Function::Diode ? Quantity::Voltage
             : f == Function::Temperature ? Quantity::Temperature : quantity(f);
    }
};
class FunctionSelector {
public:
    bool request(Function function, const Result &latest, uint32_t now, uint64_t minimumTimestampUs = 0);
    bool poll(const Result &latest, uint32_t now, CommandGroup &command);
    // A group key steps the controller within its group (DC/AC of V, mV, A and mA; Ohm, continuity,
    // diode, capacitance in turn), and it can still report the old function long after the key: a key
    // pressed again on time alone lands on the other DC/AC (the factory's restore 0804BE00/0804D2C4
    // presses on every mismatch, without a deadline: "AC/DC amps forever"). So each further key waits
    // until the last one has shown: a packet after it whose recognised function is known and differs
    // from the one the key was pressed on (an old packet, Unknown, or mA/uA within one function is
    // not). A key that never shows ends in one Timeout, not a series of blind toggles. KeySettleMs is
    // still the least spacing between keys.
    static constexpr uint32_t KeySettleMs = 900;
    static constexpr uint32_t DeadlineMs = 6500; // A selection the controller has not shown by then has failed.
    static constexpr uint32_t PowerOnWaitMs = 1000; // Most it waits for a deferred power-on first (dmm::LinkWatchdog::OffMs).
    // The key poll() returned went out: its origin is the function poll() saw (a UART still busy
    // sends nothing, and poll() prepares again).
    void sent(uint32_t sequence, uint32_t now) { sequence_ = sequence; nextAt_ = now + KeySettleMs; origin_ = prepared_; ++attempts_; }
    void cancel() { status_ = DataStatus::NoData; }
    void fail() { status_ = DataStatus::HardwareError; }
    bool busy() const { return status_ == DataStatus::Busy; }
    DataStatus status() const { return status_; }
private:
    Function requested_ = Function::Unknown;
    DataStatus status_ = DataStatus::NoData;
    uint32_t deadline_ = 0, nextAt_ = 0, sequence_ = 0;
    Function origin_ = Function::Unknown, prepared_ = Function::Unknown; // Shown when the last key went / goes out.
    unsigned attempts_ = 0;
    uint64_t minimumTimestampUs_ = 0;
};
} // namespace openosc::dmm
