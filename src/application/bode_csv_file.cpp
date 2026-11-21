#include "openosc/application/bode_csv_file.hpp"
#if OPENOSC_BOARD == 703
#include "openosc/acquisition/ranges.hpp"
#include "openosc/data/decimal.hpp"
#include "openosc/storage/directories.hpp"
#include "openosc/storage/layout.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

namespace openosc {
namespace {
// SRAM4 (NOLOAD, not zeroed): built in place on first use. ITCM (.bode, .decode_workspace) and the
// DTCM above the stack reserve have no room for it.
#ifdef OPENOSC_APPLICATION
#define BODE_CSV_STORAGE __attribute__((section(".settings_workspace")))
#else
#define BODE_CSV_STORAGE
#endif
alignas(BodeCsvFile) unsigned char fileStorage[sizeof(BodeCsvFile)] BODE_CSV_STORAGE;
BodeCsvFile *instance = nullptr;
using Point = scope::BodePoint;
constexpr struct { uint16_t flag; const char *name; } FlagNames[] = {
    {Point::ClipCh1, "clip_ch1"}, {Point::ClipCh2, "clip_ch2"}, {Point::WeakCh1, "weak_ch1"}, {Point::WeakCh2, "weak_ch2"},
    {Point::Alias, "alias"}, {Point::FewCycles, "few_cycles"}, {Point::Incomplete, "incomplete"},
    {Point::Uncalibrated, "uncalibrated"}, {Point::NoFrame, "no_frame"}, {Point::NoTone, "no_tone"}, {Point::Partial, "partial"},
};
enum Item : unsigned {
    MetaFormatLine, MetaFirmware, MetaStatus, MetaMeasured, MetaPlanned, MetaRequestedStart, MetaRequestedStop, MetaStart, MetaStop, MetaDensity, MetaSettle,
    MetaAveragesLine, MetaShape, MetaLevel, MetaLevelLabel, MetaCodesStart, MetaCodesStop, MetaReference, MetaResponse, MetaGain, MetaPhase, MetaHeaderLine, MetaFirstRow
};
#ifdef OPENOSC_VERSION
constexpr const char *FirmwareName = "OpenOSC " OPENOSC_VERSION;
#else
constexpr const char *FirmwareName = "OpenOSC";
#endif
} // namespace
BodeCsvFile &bodeCsvFile() {
    if (!instance) instance = new (fileStorage) BodeCsvFile;
    return *instance;
}
// One line into the rest of the chunk: whole or not at all (ok() false once anything did not fit).
class BodeCsvFile::Line {
public:
    Line(char *output, size_t capacity) : p_(output), capacity_(capacity) {}
    void put(const char *text) {
        const size_t n = std::strlen(text);
        if (!ok_ || n > capacity_ - used_) { ok_ = false; return; }
        std::memcpy(p_ + used_, text, n); used_ += n;
    }
    void put(char c) { const char text[2] = {c, 0}; put(text); }
    void integer(unsigned long value) { char t[24]; Decimal::integer(value, t, sizeof t); put(t); }
    void millihertz(uint64_t value) { char t[32]; if (Decimal::fraction(value, 1000, t, sizeof t, 3)) put(t); }
    void fixed(double value, unsigned decimals) { char t[48]; if (Decimal::fixed(value, decimals, t, sizeof t)) put(t); }
    void key(const char *name) { put("# "); put(name); put('='); }
    void codes(const generator::Settings &settings, uint64_t millihertz) {
        generator::Settings at = settings;
        at.frequencyHz = {uint32_t(millihertz), 1000}; // The Dedicated sine's codes change at 1 kHz.
        const auto range = generator::Levels::codes(at);
        integer(range.minimum); put('-'); integer(range.maximum);
    }
    void end() { put("\r\n"); }
    bool ok() const { return ok_; }
    size_t used() const { return used_; }
private:
    char *p_;
    size_t capacity_, used_ = 0;
    bool ok_ = true;
};
bool BodeCsvFile::fail(const char *message) {
    if (phase_ == Phase::Writing || phase_ == Phase::Finishing) { f_close(&file_); f_unlink(temporary_); }
    phase_ = Phase::Idle; saved_ = false; error_ = message;
    run_.points = nullptr;
    return false;
}
bool BodeCsvFile::start(Filesystem &fs, const BodeCsvRun &run) {
    if (busy()) return false;
    run_ = run; saved_ = false; error_ = nullptr; written_ = 0; id_ = 1; item_ = 0; final_[0] = 0;
    if (!run.points || !run.count) return fail("FRA CSV: NO POINTS");
    if (!run.grid.valid() || run.grid.count > scope::BodeSettings::MaximumPoints || run.count > run.grid.count)
        return fail("FRA CSV: INVALID RUN");
    if (!fs.mounted() || !fs.refreshFree()) return fail("STORAGE NOT AVAILABLE");
    if (!fs.freeBytes()) return fail("NOT ENOUGH FREE SPACE"); // Filling up later: the failed f_write.
    phase_ = Phase::Directory;
    return true;
}
void BodeCsvFile::cancel(const char *why) { if (busy()) fail(why); }
void BodeCsvFile::service() {
    switch (phase_) {
    case Phase::Directory:
        if (storage::ensureDirectory(storage::Layout::Fra) != FR_OK) { fail("CANNOT CREATE FRA DIRECTORY"); return; }
        phase_ = Phase::Naming;
        return;
    case Phase::Naming: name(); return;
    case Phase::Writing: write(); return;
    case Phase::Finishing: finish(); return;
    case Phase::Idle: return;
    }
}
void BodeCsvFile::name() {
    for (unsigned tried = 0; tried < NamesPerService; ++tried, ++id_) {
        if (id_ > MaximumId) { fail("FRA FILE NAME LIMIT REACHED"); return; }
        const unsigned long id = id_;
        std::snprintf(final_, sizeof final_, "%s/FRA%05lu.CSV", storage::Layout::Fra, id);
        std::snprintf(temporary_, sizeof temporary_, "%s/FRA%05lu.TMP", storage::Layout::Fra, id);
        FILINFO info{};
        FRESULT result = f_stat(final_, &info);
        if (result == FR_OK) continue;
        if (result != FR_NO_FILE) { fail("CANNOT CHECK FRA FILE NAME"); return; }
        result = f_open(&file_, temporary_, FA_WRITE | FA_CREATE_NEW);
        if (result == FR_EXIST) continue; // An unfinished one of before: its number stays taken.
        if (result == FR_DENIED) { fail("FRA CSV DISK FULL"); return; }
        if (result != FR_OK) { fail("CANNOT CREATE FRA FILE"); return; }
        phase_ = Phase::Writing;
        return;
    }
}
// As many whole lines as the chunk holds, one f_write; the step after the last line finishes the file.
void BodeCsvFile::write() {
    size_t used = 0;
    for (;;) {
        Line out(buffer_ + used, sizeof buffer_ - used);
        if (!line(item_, out)) break;
        if (!out.ok()) break;
        used += out.used(); ++item_;
    }
    if (!used) {
        Line probe(buffer_, sizeof buffer_);
        if (line(item_, probe)) { fail("FRA CSV LINE TOO LONG"); return; } // Never: a line (a row ~200 B) < Chunk.
        phase_ = Phase::Finishing;
        return;
    }
    UINT done = 0;
    const FRESULT result = f_write(&file_, buffer_, UINT(used), &done);
    if (result != FR_OK) { fail("FRA CSV WRITE FAILED"); return; }
    if (done != used) { fail("FRA CSV DISK FULL"); return; } // FatFs: no cluster left.
    written_ += done;
}
void BodeCsvFile::finish() {
    const FRESULT synced = f_sync(&file_), closed = f_close(&file_);
    phase_ = Phase::Idle;
    if (synced != FR_OK || closed != FR_OK) { f_unlink(temporary_); fail("FRA CSV SYNC FAILED"); return; }
    if (f_rename(temporary_, final_) != FR_OK) { f_unlink(temporary_); fail("FRA CSV FINALIZE FAILED"); return; }
    run_.points = nullptr;
    saved_ = true;
}
bool BodeCsvFile::line(unsigned item, Line &out) const {
    if (item < MetaHeaderLine) metadata(item, out);
    else if (item == MetaHeaderLine) out.put(Header);
    else if (item - MetaFirstRow < run_.count) row(item - MetaFirstRow, out);
    else return false;
    out.end();
    return true;
}
void BodeCsvFile::metadata(unsigned item, Line &out) const {
    const auto &s = run_.settings;
    const auto &g = run_.excitation;
    switch (Item(item)) {
    case MetaFormatLine: out.key("format"); out.put(Format); break;
    case MetaFirmware: out.key("firmware"); out.put(FirmwareName); break;
    case MetaStatus: out.key("status"); out.put(run_.complete ? "complete" : "stopped"); break;
    case MetaMeasured: out.key("points_measured"); out.integer(run_.count); break;
    case MetaPlanned: out.key("points_planned"); out.integer(run_.grid.count); break;
    case MetaRequestedStart: out.key("requested_start_hz"); out.millihertz(s.startMillihertz); break;
    case MetaRequestedStop: out.key("requested_stop_hz"); out.millihertz(s.stopMillihertz); break;
    case MetaStart: out.key("start_hz"); out.millihertz(run_.grid.start); break;
    case MetaStop: out.key("stop_hz"); out.millihertz(run_.grid.stop); break;
    case MetaDensity: out.key("points_per_decade"); out.integer(s.pointsPerDecade); break;
    case MetaSettle: out.key("settle_ms"); out.integer(s.settleMs); break;
    case MetaAveragesLine: out.key("averages"); out.integer(s.averages); break;
    case MetaShape: {
        const unsigned shape = unsigned(g.shape);
        out.key("generator_shape"); out.put(shape < sizeof generator::ShapeNames / sizeof *generator::ShapeNames ? generator::ShapeNames[shape] : "");
        break;
    }
    case MetaLevel: {
        const unsigned level = unsigned(g.level);
        out.key("generator_level"); out.put(level < sizeof generator::Levels::Names / sizeof *generator::Levels::Names ? generator::Levels::Names[level] : "");
        break;
    }
    case MetaLevelLabel: {
        const char *label = generator::Levels::label(g.level);
        out.key("generator_level_label_v"); out.put(label ? label : ""); // Custom: no factory caption.
        break;
    }
    case MetaCodesStart: out.key("generator_dac_codes_at_start"); out.codes(g, run_.grid.start); break;
    case MetaCodesStop: out.key("generator_dac_codes_at_stop"); out.codes(g, run_.grid.stop); break;
    case MetaReference: out.key("reference"); out.put("CH1"); break;
    case MetaResponse: out.key("response"); out.put("CH2"); break;
    case MetaGain: out.key("gain"); out.put("20*log10(CH2/CH1) dB"); break;
    case MetaPhase: out.key("phase"); out.put("CH2-CH1 degrees in (-180;180]"); break;
    case MetaHeaderLine: case MetaFirstRow: break;
    }
}
void BodeCsvFile::row(unsigned index, Line &out) const {
    const Point &p = run_.points[index];
    const bool valid = p.valid(), amplitudes = valid && p.ch1Vpp > 0;
    out.integer(index + 1u); out.put(','); // 1..N, as the table on screen.
    out.millihertz(run_.grid.at(index)); out.put(',');
    if (p.frequencyHz > 0) out.fixed(p.frequencyHz, 3);
    out.put(',');
    if (valid) out.fixed(p.gainDb, 3);
    out.put(',');
    if (p.phased()) out.fixed(p.phaseDeg, 2);
    out.put(',');
    if (amplitudes) out.fixed(p.ch1Vpp, 6);
    out.put(',');
    if (amplitudes) out.fixed(p.ch1Vpp * std::pow(10.0, p.gainDb / 20.0), 6);
    out.put(',');
    // One word, the worst first: invalid, weak_ch2 (no phase), partial (fewer records averaged), ok.
    out.put(!valid ? "invalid" : (p.flags & Point::WeakCh2) ? "weak_ch2" : (p.flags & Point::Partial) ? "partial" : "ok");
    out.put(',');
    out.integer(p.flags); out.put(',');
    bool first = true;
    for (const auto &f : FlagNames)
        if (p.flags & f.flag) { if (!first) out.put('|'); out.put(f.name); first = false; }
    for (unsigned c = 0; c < 2; ++c) {
        out.put(',');
        if (const uint32_t mv = scope::VoltageRanges::millivolts(p.range[c])) out.integer(mv);
    }
}
} // namespace openosc
#endif
