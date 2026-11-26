#pragma once
#include "openosc/decode/result.hpp"

namespace openosc::decoding {
// The decoder's texts (plan §4.2, §4.3), all fixed width in openosc_mono_12 (every glyph the same
// advance, 115/16 px: a row's width is its character count; 43 characters fit the 316 px a row has).
//   Table row "M TTTTTTTTT IIII DDDDDDDDDDDDDDDDDDDD SSSS": the selection marker '>', the time (one unit for
//   the whole record, in the header; from the trigger, or from the record's start "t0" when it was not
//   triggered), ID, DATA (cut with '~': mono_12 has no '…'; '+' first for a CONT segment) and the status
//   code (plan §4.3: ok PE FE BRK NAK STF CRC ACK FRM CHK SYN LEN? CUT ? and N/C NOT CHECKED, CONT, Fs?,
//   EVT> TRUNCATED). CAN records with a 29-bit ID take 8 ID and 16 DATA characters. An ISO CAN FD
//   frame's DATA starts with FD (no BRS) / FB (BRS), 'e' added for a recessive ESI ("FBe 11 22").
//   Detail: one event whole, line by line (indices, time, Fs, the edges' uncertainty, flags, the
//   protocol's fields, the levels used, the result's quality in full, every element in HEX and ASCII, eight a line: a 64-byte FD frame
//   takes eight). CAN frames are read through protocols::CanFdView only: format, ID, BRS/ESI/RRS/res
//   (an FD frame has no RTR), both rates, the CRC whole (CRC-15/17/21 in 4/5/6 digits), the stuff count
//   and where the reception stopped.
//   Info: the state and the counters of the result on show.
// Pure text, no LVGL: the widget draws what this writes, at draw time.
class TableText {
public:
    static constexpr unsigned Time = 9, Status = 4, Columns = 41, Capacity = 44;
    using Line = char[Capacity];
    struct Unit {
        const char *name = "s";
        int exponent = 0; // 10^exponent seconds.
    };
    // The record's time unit: the smallest of ns, us, ms, s in which both ends of the record, from
    // its time origin, fit Time characters with two decimals.
    static Unit unit(const ResultSet &set);
    static void header(const ResultSet *set, const Unit &unit, Line &out);
    static void row(const ResultSet &set, unsigned index, const Unit &unit, bool selected, Line &out);
    // Detail of event `index`, line `line` (false past its last line).
    static bool detail(const ResultSet &set, unsigned index, const Settings &settings, unsigned line, Line &out);
    // The status code of an event (plan §4.3), at most Status characters: from its descriptor alone.
    static const char *code(const DecodeEvent &event);
    // The code of event `index` as the table, the strip and Detail show it: an ISO CAN FD frame whose
    // CRC matched but whose stuff count failed (parity or count) is SBC, not CRC (CanFdView).
    static const char *code(const ResultSet &set, unsigned index);
    static bool error(const DecodeEvent &event); // An error code (not ok, CONT, ?, N/C, LEN?, Fs?).
    // Wave + annotations: a short label of event `index` (cut to its frame by the strip): an error's
    // code, else the event's ID or first element ("48", "50W", "ID12", "123", "BRK"); an ISO CAN FD
    // frame its ID, mark and data length ("123 FB 64B").
    static void label(const ResultSet &set, unsigned index, char *out, unsigned capacity);
    // The counters a result line shows, besides the result itself.
    struct Counters {
        uint32_t recordsPerSecondX10 = 0, ageMs = 0, skipped = 0;
        bool age = false; // ageMs is known (a result was published).
        bool exporting = false; // A result held by an export: the decoder waits (EXPORT).
    };
    // The result line, at most InfoWidth characters: it is the Decoder-only info row and, after the time
    // column's name ("t0,ms "), the Split title. In this order: the state (LEVELS n% while the Auto
    // hysteresis measures the swing, DEC n%, WAIT or a status; a result done is said by its cov100%), EXPORT, cov n%, the quality words (noCS for every
    // SPI result, EVT>n, OVRn, CLIP, FsLOW, GLn glitches), rec/s, events, skip, age. The state, EXPORT,
    // cov and every quality word are always there; rec/s, ev, skip, age only while they fit (dropped in
    // the reverse order); short of room the quality counts are capped (99+), then left out, then those
    // words take their short forms (D n%, L n%, EXP, nCS, E>, OV, CL, FsL, SRC OFF).
    static constexpr unsigned InfoWidth = Capacity - 1 - 6;
    static void info(const Settings &settings, const ResultSet *shown, const ResultSet *building, const Counters &counters,
                     Line &out);
    // The empty table's line: why there is no event (no record yet, its status, or none in it).
    static const char *empty(const ResultSet *shown, bool decoding);
    // A published result's state in the info line's word ("SOURCE OFF", brief "SRC OFF"; WAIT for one
    // not decoded yet or cancelled), "" for Done: also Save CSV's value when there is nothing to save.
    static const char *statusWord(DecodeStatus status, bool brief);
    // A value with three significant digits and an SI prefix ("850ns", "12.4us", "100MS/s").
    static void quantity(double value, const char *unit, char *out, unsigned capacity);
    static void millivolts(int64_t mv, char *out, unsigned capacity); // "-1.650V" (any int32 or uint32 mV)
};
} // namespace openosc::decoding
