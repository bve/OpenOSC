#include "openosc/application/readout_scheduler.hpp"
#include "openosc/instrument/data_access.hpp"
#include "openosc/ui/application_view.hpp"
#include "openosc/ui/scope/screen_readouts.hpp"

namespace openosc {
void ReadoutScheduler::suspend(DataAccess &data) {
    if (record_.running()) record_.cancel();
    data.releaseReadout();
    painted_ = false;
}
void ReadoutScheduler::reset(DataAccess &data) {
    suspend(data);
    shown_ = {}; configured_ = painted_ = false; percent_ = 255; projection_ = 0;
}
void ReadoutScheduler::counterFallback(DataAccess &data, const Rational &samplesPerSecond) {
    if (!selection_.shortWindowCounter || !shown_.valid || !samplesPerSecond.valid()) return;
    const auto counters = data.scopeCounters();
    const double window = shown_.points / samplesPerSecond.value();
    for (unsigned c = 0; c < 2; ++c)
        if (shown_.computed[c]) scope::CounterFallback::apply(shown_.values[c], counters.channels[c], shown_.timestampUs, window);
}
void ReadoutScheduler::publish(ApplicationView &view, uint32_t now) {
    ui::ReadoutState state;
    state.running = record_.running();
    state.percent = record_.percent();
    state.older = shown_.valid && shown_.sequence != displayed_;
    state.ageMs = shown_.valid ? now - shown_.finishedAt : 0;
    painted_ = view.scopeReadouts(shown_, state);
    published_ = displayed_; percent_ = state.percent; publishedAt_ = now;
}
void ReadoutScheduler::service(DataAccess &data, ApplicationView &view, const scope::ReadoutSelection &selection, uint32_t now) {
    const auto &model = view.waveform().model();
    if (!model.hasFrame()) { reset(data); return; }
    const bool changed = !configured_ || !selection.same(selection_);
    if (changed) { suspend(data); selection_ = selection; configured_ = true; shown_ = {}; }
    displayed_ = model.metadata().sequence;
    if (!view.scopeReadoutsPainted()) painted_ = false; // A panel closed and the screen cleared them.
    const bool due = changed || uint32_t(now - computedAt_) >= PeriodMs;
    const bool timeView = model.options().mode == plot::Mode::Time || model.options().mode == plot::Mode::SpectrumOverlay;
    if (selection.basis == scope::ReadoutBasis::Screen && timeView) {
        if (record_.running()) record_.cancel(); // A Y-T view never falls back to a full scan,
        data.releaseReadout();                   // not even for an empty window.
        const auto &screen = model.screen();
        if (changed || (screen.projection != projection_ && due)) {
            shown_ = plot::ScreenReadouts::calculate(screen, model.metadata(), selection, now);
            counterFallback(data, model.metadata().samplesPerSecond);
            projection_ = screen.projection; computedAt_ = now; ++screenRuns_;
            publish(view, now);
        } else if (!painted_ || published_ != displayed_) publish(view, now); // Cached, or pending for a newer record.
        return;
    }
    record(data, view, selection, now, due);
}
void ReadoutScheduler::record(DataAccess &data, ApplicationView &view, const scope::ReadoutSelection &selection, uint32_t now, bool due) {
    if (record_.running()) {
        const auto *frame = data.readoutRevision() == revision_ ? data.readoutFrame(record_.sequence()) : nullptr;
        if (!frame || data.readoutRevision() != revision_) { // Revoked: from the beginning, at the next due pass.
            record_.cancel(); data.releaseReadout();
            publish(view, now);
            return;
        }
        const bool done = record_.step(*frame, SlicePoints, now);
        if (!record_.running()) {
            const auto rate = frame->metadata().samplesPerSecond; // The record's, before its lease goes.
            data.releaseReadout();
            if (done) { shown_ = record_.result(); counterFallback(data, rate); computedAt_ = now; ++recordRuns_; }
            publish(view, now);
        } else if (record_.percent() != percent_ && uint32_t(now - publishedAt_) >= ProgressMs) publish(view, now);
        return;
    }
    if (shown_.valid && shown_.basis == scope::ReadoutBasis::Record && shown_.sequence == displayed_) {
        if (!painted_ || published_ != displayed_) publish(view, now);
        return; // This record is done: neither HOLD pan nor cursors start it again.
    }
    if (!due) {
        if (!painted_ || published_ != displayed_) publish(view, now);
        return;
    }
    const auto *frame = data.readoutFrame(displayed_); // Only the record on display.
    if (!frame) return;
    revision_ = data.readoutRevision();
    record_.start(*frame, selection);
    if (!record_.running()) { data.releaseReadout(); return; }
    if (shown_.basis != scope::ReadoutBasis::Record) shown_ = {}; // No Screen values under a Full label.
    shown_.basis = scope::ReadoutBasis::Record;
    publish(view, now);
}
} // namespace openosc
