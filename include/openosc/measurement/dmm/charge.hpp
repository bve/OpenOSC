#pragma once
#include "openosc/measurement/dmm/result.hpp"
#include <cstddef>
#include <cstdint>

namespace openosc::dmm {
enum class Function : unsigned; // function.hpp
// The ampere-hour counter of the DC current functions (GitHub #132): the signed charge of the readings
// the DMM sends, integrated over their packet times (Session::accept's timestamps, the DataAccess
// clock built from the HAL millisecond tick, monotonic across its wrap). Each interval between two
// consecutive DC current readings of one function, both valid and at most MaximumGapUs apart, adds
// their mean current times its length (the trapezoid rule). Any other packet ends the run: an
// overload, no reading, AC, another function, a packet of an unconfirmed selection, the DMM's own
// HOLD or REL (their values are not the current), or a gap longer than MaximumGapUs; so does every
// known loss of readings, however short (interrupt(): a UART error or restart, a rejected or
// undecodable packet, readings expired): no current is assumed where none was read, and the charge
// stays. Only a confirmed packet of another function or input resets it, and leaving or entering the
// DMM screen (reset()); a selection that is repeated, refused or not yet confirmed keeps it. A range
// change keeps it (values are SI). The local REL, HOLD and the hold modes only change what the screen
// shows: the counter reads the packets.
class ChargeCounter {
public:
    static constexpr uint64_t MaximumGapUs = 2000000; // The DMM's readings go stale after 2 s (DataAccess).
    // One decoded packet. `confirmed` false: a packet of an awaited selection not yet confirmed, which
    // neither resets nor counts (it ends the run). Its function, if another, resets the charge.
    void observe(const Result &result, uint64_t timestampUs, bool confirmed);
    void reset();
    void interrupt() { linked_ = false; } // Readings were lost: the next one starts a new run, the charge stays.
    // Whether `function` is the one counted (no function yet: none is).
    bool counting(Function function) const { return function_ == unsigned(function); }
    double coulombs() const { return coulombs_; }
    double ampereHours() const { return coulombs_ / 3600; }
    // Whether the function shows the counter: DC current, either input (A, mA/uA).
    static bool counts(Function function);
    // "12.34 mAh", "-1.234 Ah": four significant digits, mAh below 1 Ah, Ah from there; "--" when the
    // text does not fit. Fixed decimals per decade, so the width changes only with the magnitude.
    static void format(double ampereHours, char *output, size_t capacity);

private:
    double coulombs_ = 0, amps_ = 0; // The charge so far and the last integrable reading's current.
    uint64_t atUs_ = 0;              // That reading's packet time.
    unsigned function_ = ~0U;        // The function the charge belongs to (~0: none yet).
    bool current_ = false, linked_ = false; // Its input; whether the next reading may join the last.
};
} // namespace openosc::dmm
