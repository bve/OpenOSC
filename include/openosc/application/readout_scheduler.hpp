#pragma once
#include "openosc/acquisition/readouts.hpp"
#include "openosc/data/status.hpp"

namespace openosc {
class ApplicationView;
class DataAccess;
// Numeric readouts beside the waveform, on their own period. Only the values selected are computed.
// Screen basis (every Y-T view): from the projection's compact series, again only when a new
// projection exists (a frame, a pan or zoom) and the period has passed; cursors change nothing;
// a window with no record points shows no values. Record basis, and X-Y/FFT views (no time
// projection): every point of the record on display, a bounded slice per display pass, through
// DataAccess::readoutFrame(): DataAccess revokes that lease itself before anything that needs the
// memory, and a revoked job starts again from its beginning. A finished record result is kept, so
// HOLD pan and cursors do not start it again.
class ReadoutScheduler {
public:
    // SlicePoints: record positions per display pass (both channels at each). ProgressMs: a running
    // job's percent is painted again at most this often; its start, end and revocation at once.
    static constexpr uint32_t PeriodMs = 250, SlicePoints = 32768, ProgressMs = 100;
    void service(DataAccess &data, ApplicationView &view, const scope::ReadoutSelection &selection, uint32_t now);
    // A display pass that does not service readouts (panel, settings, hidden waveform): the
    // running job stops and its lease is given back; finished results stay and are painted again
    // when readouts are serviced next.
    void suspend(DataAccess &data);
    // Leaving the Scope: everything is forgotten.
    void reset(DataAccess &data);
    // Telemetry (native tests, SWD review): Screen results calculated, Record results completed.
    uint32_t screenCalculations() const { return screenRuns_; }
    uint32_t recordCalculations() const { return recordRuns_; }
    bool recordRunning() const { return record_.running(); }
    const scope::ReadoutResult &shown() const { return shown_; }

private:
    void publish(ApplicationView &view, uint32_t now);
    void record(DataAccess &data, ApplicationView &view, const scope::ReadoutSelection &selection, uint32_t now, bool due);
    // GitHub #171 (Short window f/T: From the counter): the f/T a just computed shown_ cannot resolve,
    // from the count of its record's moment (CounterFallback, its timestampUs), once: a cached result
    // (HOLD pan, a record done) keeps what its moment had, a new one is judged by its own.
    void counterFallback(DataAccess &data, const Rational &samplesPerSecond);
    scope::RecordReadouts record_;
    scope::ReadoutResult shown_;
    scope::ReadoutSelection selection_;
    uint32_t computedAt_ = 0, projection_ = 0, displayed_ = 0, published_ = 0, revision_ = 0, publishedAt_ = 0;
    uint32_t screenRuns_ = 0, recordRuns_ = 0;
    uint8_t percent_ = 255;
    // The labels show shown_: false after a suspend (a panel may have cleared them), a reset, or a
    // paint the view refused, so the cached result is painted again without being recomputed.
    bool configured_ = false, painted_ = false;
};
} // namespace openosc
