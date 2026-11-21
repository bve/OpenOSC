#pragma once
#include "openosc/acquisition/bode.hpp"
#include "openosc/generator/waveform.hpp"
#include "openosc/storage/filesystem.hpp"
#include <cstddef>
#include <cstdint>

namespace openosc {
// What a finished or stopped frequency response run was (GitHub #72): its settings and clamped grid as
// they ran, the generator's settings that played (its level and DAC codes; the frequency is each point's),
// the measured points (count of them, the first count of the grid) and whether the sweep reached the end.
// The points are the caller's and must stay unchanged until the file is no longer busy().
struct BodeCsvRun {
    scope::BodeSettings settings;
    scope::BodeGrid grid;
    generator::Settings excitation;
    const scope::BodePoint *points = nullptr;
    unsigned count = 0;
    bool complete = false;
};
// A run's CSV into /openosc/FRA/FRAnnnnn.CSV (format openosc-fra-csv-v1: "# key=value" metadata lines,
// one header row, one row a point), its directory created on first use. start() checks the run and the
// volume and only queues the work; service(), a bounded step a main-loop pass, then: makes the directory
// (one step), looks for a free number (NamesPerService names a step), creates FRAnnnnn.TMP with
// FA_CREATE_NEW (an existing name is never overwritten; an unfinished .TMP keeps its number taken),
// writes at most Chunk bytes of whole lines a step, then (its own step) syncs, closes and renames: the
// final name appears only for a whole file. Any write, sync or rename error, a full volume or a cancel
// closes and removes the temporary file, says why (error()) and ends the export (a medium that still
// refuses writes refuses that removal too: the .TMP then stays, never a final name).
// Values: numbers in fixed point through Decimal (no %f: newlib-nano); a cell is empty when it says
// nothing: an invalid point has no gain, phase or amplitudes, a weak CH2 no phase, an unknown CH1
// amplitude no amplitudes. quality: invalid, weak_ch2, partial (fewer valid records than averaged) or ok;
// flags: BodePoint's bits, in decimal and as names joined by '|'.
// The instance lives in SRAM4 (.settings_workspace on the MCU: ITCM and DTCM have no room for it).
// Chunk is the smallest sector: a write of whole lines is shorter, so FatFs copies it through its window
// (the BlockDevice copies sectors into its own scratch), never a transfer from this buffer.
class BodeCsvFile {
public:
    static constexpr unsigned Chunk = 512, NamesPerService = 8, MaximumId = 99999;
    static constexpr const char *Format = "openosc-fra-csv-v1";
    static constexpr const char *Header =
        "index,nominal_hz,frequency_hz,gain_db,phase_deg,ch1_vpp,ch2_vpp,quality,flags,flag_names,ch1_mv_per_div,ch2_mv_per_div";
    // Queues the run's file. False (nothing queued; error() says why) for a run with no points, a
    // malformed one, an unmounted volume or one with no free space (a volume that fills up while written
// ends the file at the failed f_write); false with nothing changed while busy().
    bool start(Filesystem &fs, const BodeCsvRun &run);
    void service();
    bool busy() const { return phase_ != Phase::Idle; }
    const char *savedPath() const { return saved_ ? final_ : nullptr; } // The last file, once whole.
    const char *error() const { return error_; }                       // The last failure, or nullptr.
    void cancel(const char *why = "FRA CSV CANCELLED");                // Idempotent: nothing when idle.
    uint32_t written() const { return written_; }

private:
    enum class Phase : uint8_t { Idle, Directory, Naming, Writing, Finishing };
    class Line;
    bool fail(const char *message);
    void name();
    void write();
    void finish();
    bool line(unsigned item, Line &out) const; // False past the last line.
    void metadata(unsigned item, Line &out) const;
    void row(unsigned index, Line &out) const;
    BodeCsvRun run_{};
    FIL file_{};
    uint32_t written_ = 0, id_ = 0;
    unsigned item_ = 0; // The next line to write.
    Phase phase_ = Phase::Idle;
    bool saved_ = false;
    const char *error_ = nullptr;
    char temporary_[32]{}, final_[32]{};
    char buffer_[Chunk];
};
BodeCsvFile &bodeCsvFile(); // The one instance (SRAM4 on the MCU).
} // namespace openosc
