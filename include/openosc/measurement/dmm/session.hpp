#pragma once
#include "openosc/measurement/dmm/charge.hpp"
#include "openosc/measurement/dmm/display_prefix.hpp"
#include "openosc/measurement/dmm/hold.hpp"
#include "openosc/measurement/dmm/result.hpp"
#include "openosc/measurement/dmm/relative.hpp"
#include "openosc/measurement/dmm/temperature.hpp"
#include <optional>
#include <cstddef>
#include <cstring>

namespace openosc::dmm {
enum class Function : unsigned; // function.hpp
// Running statistics of the factory firmware: min, max and sum/count over every valid reading
// since the last reset, no time window and no cap (the trend's 260 slots are separate). Values
// are SI, so a range change alone keeps them. docs/DMM_STATISTICS.md has the reset matrix.
// Each value also keeps the quantum it is known to (GitHub #168: DisplayPrefix::exponent of the
// readings' Resolution): MIN and MAX their own reading's, AVG the coarsest of the period's (Unknown
// when any reading had none), so the screen never shows them finer than the readings across a range
// change. The sums stay exact.
class Statistics {
public:
    bool push(const NumericValue &value, int8_t exponent = DisplayPrefix::Unknown);
    void reset() {
        count_ = 0;
        min_ = max_ = sum_ = correction_ = 0;
        quantity_ = Quantity::Unknown;
        minimumExponent_ = maximumExponent_ = averageExponent_ = DisplayPrefix::Unknown;
    }
    uint64_t count() const { return count_; }
    double minimum() const { return min_; }
    double maximum() const { return max_; }
    double average() const { return count_ ? sum_ / count_ : 0; }
    Quantity quantity() const { return quantity_; }
    int8_t minimumExponent() const { return minimumExponent_; }
    int8_t maximumExponent() const { return maximumExponent_; }
    int8_t averageExponent() const { return averageExponent_; }

private:
    uint64_t count_ = 0;
    double min_ = 0, max_ = 0, sum_ = 0, correction_ = 0;
    Quantity quantity_ = Quantity::Unknown;
    int8_t minimumExponent_ = DisplayPrefix::Unknown, maximumExponent_ = DisplayPrefix::Unknown,
           averageExponent_ = DisplayPrefix::Unknown;
};
// MIN/MAX/AVG as the DMM screen shows them: the period's values when they are of the shown
// reading's quantity (else none), less the local REL offset (RelativeReading::offset; a controller
// REL is already in the values). One view for the screen and SCPI, no second accumulator.
struct StatisticsView {
    bool valid = false;
    uint64_t count = 0;
    double minimum = 0, maximum = 0, average = 0;
    Quantity quantity = Quantity::Unknown;
    int8_t minimumExponent = DisplayPrefix::Unknown, maximumExponent = DisplayPrefix::Unknown,
           averageExponent = DisplayPrefix::Unknown; // Statistics' quanta.
    static StatisticsView of(const Statistics &statistics, Quantity shown, double offset);
};
class History {
public:
    static constexpr size_t Capacity = 260;
    void push(double value);
    bool value(size_t oldestIndex, double &output) const;
    size_t size() const { return size_; }
    void reset() { cursor_ = size_ = 0; }

private:
    double samples_[Capacity]{};
    size_t cursor_ = 0, size_ = 0;
};
// The factory's resistance settling after an overload (dmm_update_display 703S 0803C514 / 706S
// 0803CDB0, flag 0x20001026 / 0x20001056, counter 0x20001027 / 0x20001057): an overload in an Ohm unit
// arms it, and the next Skipped readings in an Ohm unit without it are not shown, so the digits
// stay on OL while the input settles. Unlike the factory's: an overload re-arms it in full (there a
// counter already running continues), and the Session clears it on a function change or selection
// (the factory's flag outlives them) and does not advance it under HOLD (there no packet is decoded).
class OverloadSettle {
public:
    static constexpr unsigned Skipped = 2;
    bool skip(const Result &result) { // True: this reading is not shown.
        if (result.primary.quantity != Quantity::Resistance) return false;
        if (result.primary.status == DataStatus::Overload) { left_ = Skipped; return false; }
        if (!left_) return false;
        if (result.range == RangeMode::Manual) { left_ = 0; return false; } // No range hunt to settle: shown at once.
        --left_;
        return true;
    }
    void clear() { left_ = 0; }
private:
    unsigned left_ = 0;
};
// The chip's first reading after it changes the range itself (AUTO) is wrong: 0 V, or the old digits at
// the new range's scale (23.03 V stepping 2.2 -> 4.5 V), seen at the 2.5 V and the 25 mA switches on the
// bench. That reading, the first in another range of the same function (Ranges::current) as the chip
// sends it (a thermocouple's millivolts, not the temperature), is not shown, as an overload's settling.
// An overload is always shown, a manual range is the user's and is shown at once, and never two in a
// row: a signal that keeps the chip switching shows its readings rather than freezing on an old one.
class RangeSettle {
public:
    bool skip(const Result &result); // True: this reading is not shown.
    void clear() { index_ = ~0U; skipped_ = false; }
private:
    unsigned index_ = ~0U;
    bool skipped_ = false;
};
class Session {
public:
    bool accept(const uint8_t *packet, size_t bytes, uint64_t timestampUs);
    bool hold(bool enabled);
    bool held() const { return userHold_ || latest_.packetHold; }
    bool userHeld() const { return userHold_; }
    DataStatus status(uint64_t nowUs, uint64_t staleAfterUs) const;
    // Readings before nowUs are stale (an activation, a resume): the charge's run ends there too.
    void expire(uint64_t nowUs) { freshFromUs_ = nowUs; charge_.interrupt(); }
    // The last packet as decoded: its function, range, HOLD and REL flags for the commands, and the
    // raw stream (log, serial output). Settling readings (OverloadSettle) are here too.
    const Result &latest() const { return latest_; }
    const Result &live() const { return shown_; }
    const TemperatureSettings &temperatureSettings() const { return temperatureSettings_; }
    bool typeK() const { return typeK_; }
    void startupJunction(std::optional<int16_t> reading) { startupJunction_ = reading; }
    bool liveJunction(std::optional<int16_t> reading) {
        if (liveJunction_ == reading) return false;
        liveJunction_ = reading;
        return true;
    }
    std::optional<int16_t> mcuJunction() const {
        switch (temperatureSettings_.source) {
        case JunctionSource::McuStartup: return startupJunction_;
        case JunctionSource::McuLive: return liveJunction_;
        default: return {};
        }
    }
    void configureTemperature(TemperatureSettings settings, uint64_t nowUs);
    void typeK(bool enabled, uint64_t nowUs);
    // The newest packet that decoded, as received (SYST:DMM:PACK?); 0 bytes before the first.
    size_t packet(const uint8_t *&data) const { data = raw_; return rawBytes_; }
    // What the screen, the needle and SCPI show: the manual HOLD's reading, else the Delayed/Stable/
    // Auto snapshot, else the last reading shown (the latest but for settling ones). held() stays the
    // manual and the DMM's own HOLD only: the statistics, the trend and the log go on under a mode's
    // snapshot (docs/DMM_AUTO_HOLD.md).
    const Result &displayed() const {
        if (frozen_) return held_;
        const Result *snapshot = detector_.captured();
        return snapshot ? *snapshot : shown_;
    }
    // The hold mode's cycle (HoldDetector): configure (cancels it), arm, cancel, service.
    void configureHold(const HoldSettings &settings) { detector_.configure(settings); }
    bool armHold(uint64_t nowUs) { return detector_.arm(nowUs); }
    void cancelHold() { detector_.cancel(); }
    void service(uint64_t nowUs) { detector_.service(nowUs); }
    // An accepted range command (RANGE, AUTO): a new scale, the mode's window starts again; a
    // snapshot stays (Stable/Delayed keep it, Auto compares it in SI).
    void rangeCommand() { detector_.restartWindow(); }
    const HoldDetector &holdCycle() const { return detector_; }
    bool holding() const { return frozen_ || detector_.captured(); } // Something other than the latest is shown.
    const Statistics &statistics() const { return stats_; }
    const History &history() const { return history_; }
    const RelativeReading &relative() const { return relative_; }
    // The DC current's ampere-hour counter (ChargeCounter): fed by every decoded packet, HOLD or not;
    // reset by a confirmed packet of another function and by entering or leaving the DMM (resetCharge,
    // DataAccess and the controller); interruptCharge: readings were lost (UART, framing), the charge stays.
    const ChargeCounter &charge() const { return charge_; }
    void resetCharge() { charge_.reset(); }
    void interruptCharge() { charge_.interrupt(); }
    // An accepted REL command restarts the statistics, as the factory's, even when REL was already so.
    // A hold mode's cycle ends first, so REL takes the live reading as its reference, never the mode's
    // snapshot: with the live reading not valid (OL, "--.") REL is refused and the cycle, its snapshot
    // and the statistics stay as they were. Under the manual or the DMM's own HOLD, as before, the held
    // reading; REL already on needs no new reference (the statistics restart, as always).
    bool relative(bool enabled) {
        if (enabled && !relative_.active() && !frozen_ && detector_.active() && !shown_.primary.valid()) return false;
        detector_.cancel();
        if (enabled != relative_.active()) {
            if (enabled) { if (!relative_.capture(displayed())) return false; }
            else relative_.clear();
        }
        resetStatistics(); return true;
    }
    void resetStatistics() {
        stats_.reset();
        history_.reset();
    }
    // An accepted function selection: the statistics restart, and only readings of `function`
    // (or any, once the packet time passes `untilUs`, when the selection did not take) seed them;
    // packets of the old function still in flight do not.
    void restart(Function function, uint64_t untilUs);
    // An accepted group key (a physical F1-F4): the statistics, the overload settling and a hold mode's
    // snapshot restart (the ampere-hour counter waits for the packets' function); no function is awaited (the readings of the old one still in flight count until
    // the controller's function change restarts the statistics again, as the factory's).
    void reselect();
    Function awaited() const; // The selection's target while awaited, else Unknown.
    bool awaitingFunction() const { return awaiting_; }
    // The factory's no-reading placeholder: "--." in the first three characters.
    static bool placeholder(const Result &result) { return !std::strncmp(result.mainText, "--.", 3); }

private:
    void resetTemperatureReading(uint64_t nowUs);
    Result latest_, shown_, held_;
    uint8_t raw_[18]{}, rawBytes_ = 0; // Packet::Bytes.
    OverloadSettle settle_;
    RangeSettle rangeSettle_;
    HoldDetector detector_;
    Statistics stats_;
    History history_;
    ChargeCounter charge_;
    RelativeReading relative_;
    TemperatureSettings temperatureSettings_;
    std::optional<int16_t> startupJunction_, liveJunction_;
    // The context of the accumulated period: the last reading taken outside HOLD.
    unsigned function_ = ~0U;        // Its decoded Function (~0: none yet).
    bool functionCurrent_ = false, contextPeerRelative_ = false, contextKnown_ = false;
    Function awaited_{};
    uint64_t awaitUntilUs_ = 0, freshFromUs_ = 0;
    Quantity trendQuantity_ = Quantity::Unknown; // The trend restarts on a quantity of its own.
    uint32_t sequence_ = 0;
    bool userHold_ = false, frozen_ = false, awaiting_ = false;
    bool typeK_ = false;
};
// The controller's two-byte commands. AUTO/RANGE is the factory's KEY5 stroke (RangeStroke): NextRange
// 20 20 (press), RangeRelease 21 21 (short release), AutoRange 22 22 (hold).
enum class CommandGroup : uint8_t { Voltage, ResistanceGroup, Current, Millivolts, Milliamps,
                                  PeerHold, AutoRange, NextRange, PeerRelative, RangeRelease };
class Command {
public:
    static bool encode(CommandGroup group, uint8_t (&bytes)[2]);
};
} // namespace openosc::dmm
