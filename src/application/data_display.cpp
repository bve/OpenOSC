#include "openosc/application/controller.hpp"
#include "openosc/decode/table_text.hpp"
#include "openosc/ui/scope/column_cache.hpp"
#include "openosc/ui/scope/format.hpp"
#include "openosc/platform/clocks.hpp"
#include <cstdio>
#include <cstring>

namespace openosc {
namespace {
// While a selected function is awaited (boot, restore, SCPI), the chip's old reading is not shown.
const dmm::Result &shownReading(const dmm::Session &meter, dmm::Function selecting) {
    static const dmm::Result none{};
    return selecting != dmm::Function::Unknown ? none : meter.displayed();
}
} // namespace
// The function a running selection awaits, unless a HOLD keeps a reading.
dmm::Function ApplicationController::meterSelecting() const {
    const auto &meter = data_.meter();
    return data_.dmmSelectionStatus() == DataStatus::Busy && !meter.holding() ? meter.awaited() : dmm::Function::Unknown;
}
// Full-rate capture restarts under a large redraw, or while a host enumerates, have coincided with
// USB33RDY dips and bus resets on the device (cause not established). A hidden trace needs no new
// frames, and such a record takes under a millisecond when one is asked for, so those restarts wait.
// A calibration consumes records behind its opaque page: a zero is never hidden; the interleave
// offset measurement neither, nor held by the settle window its own confirmation left. Both still
// wait while a host enumerates (bounded; a measurement past its deadline ends as Timeout).
void ApplicationController::pauseHiddenCapture(uint32_t now, bool enumerating) {
    const bool measuring = data_.interleaveMeasurement().active()
#if OPENOSC_BOARD == 703
        || (panel_ == Panel::InterleaveMatch && interleaveMatch_.step() == calibration::InterleaveMatchWizard::Step::Measuring)
#endif
        || panel_ == Panel::GainMatch // Live capture is the wizard's source.
        || bodeRunning() // A sweep takes records behind its own plot.
        ;
    if (uiReady_ && view_.overlayActive() && !data_.zeroBusy() && !measuring && !autoBusy() &&
        data_.scopeRequest().trigger.mode == scope::TriggerMode::Auto) hiddenAt_ = now;
    const bool settling = (!measuring && uint32_t(now - hiddenAt_) < OverlaySettleMs) || enumerating;
    data_.pauseCapture(state_.mode == MeasurementMode::Oscilloscope && settling && data_.fullRateCapture(), now);
}
// The level belongs to the next capture; its marker sits on the scale of the record on screen.
void ApplicationController::showTrigger(const scope::FrameMetadata *shown) {
    const auto &trigger = data_.scopeRequest().trigger;
    const unsigned source = unsigned(trigger.channel);
    const auto &settings = data_.scopeMetadata().channels[source];
    const auto threshold = trigger.comparatorLevel();
    view_.waveform().trigger(trigger.channel, shown ? scope::TriggerLevel::inFrame(threshold, settings, shown->channels[source]) : double(threshold));
    // Each refresh and frame: a new source, mode, invert or scale (also staged in HOLD) leaves the limit.
    view_.waveform().triggerLimit(triggerLimit(data_.scopeRequest()));
    view_.scopeTrigger(trigger, settings);
}
// One decision for F1-F4's captions, from what the key handlers act on: Remote locks every local
// key; rows (settings, panels, gain) go Back, or Cancel the field being edited; a confirmation and a
// calibration Cancel; the image list Deletes (when there is an image) and goes Back, the preview also
// changes its size (F2); the generator's panels keep their own keys. Nothing in front: the screen's.
void ApplicationController::refreshSoftKeys() {
    if (!uiReady_) return;
    using Keys = ui::SoftKeys;
    const bool editing = navigator_.mode() != ui::PropertyNavigator::Mode::Browse;
    Keys keys;
    if (remote_) keys = Keys::locked();
    else if (state_.settingsOpen) keys = Keys::list(editing && !settingsMenu_.information());
    else if (panel_ == Panel::Confirmation) keys = Keys::cancel("");
    else if (panel_ == Panel::Zero) keys = editing ? Keys::list(true) : Keys::cancel("Calibration");
    else if (panel_ == Panel::InterleaveMatch || panel_ == Panel::GainMatch) keys = Keys::cancel("");
    else if (panel_ == Panel::Bode && view_.bode().visible()) {
        // Run waits for a restore still unconfirmed (a ZT706S acknowledge): no sweep over it.
        keys.keys[0] = bodeRunning() ? ui::SoftKey{"Stop", "Sweep", true} : ui::SoftKey{"Run", "Sweep", !bodeRestoring()};
        keys.keys[1] = {"Setup", "", !bodeRunning()};
        // F3 names the view it goes to: Results and Table back to the plot; the plot's graphs, then the table.
        const auto &shown = view_.bode().data();
        const char *next = shown.results || shown.cursor.table ? "Plot" : shown.graphs == ui::BodeGraphs::Both ? "Gain"
            : shown.graphs == ui::BodeGraphs::Gain ? "Phase" : "Table";
        keys.keys[2] = ui::SoftKey{next, "View", true};
        keys.keys[3] = {"Exit", "Bode", true};
    }
    else if (panel_ == Panel::Images) keys = imageSoftKeys();
    else if (panel_ == Panel::Sessions) keys = sessionSoftKeys();
    else if (panel_ == Panel::Generator || panel_ == Panel::Frequency) {
        using Generator = ui::GeneratorKeys::Panel;
        ui::GeneratorKeys generator;
        if (panel_ == Panel::Frequency) generator = {Generator::Frequency, true, frequencyEditor_.unit()};
        else generator = {generatorEditor_.wave() ? Generator::Wave : Generator::Params, editing, nullptr};
        view_.softKeys(&view_.generatorKeys(generator));
        return;
    } else if (panel_ != Panel::None) keys = Keys::list(editing);
    else if (decoderScreen()) { // Plan §4.4: Protocol, View, Jump (HOLD only), back to the Scope or Detail back.
        static constexpr const char *views[] = {"Wave+Ann", "Split", "Table"};
        const auto &d = prefs_.scope.decoder;
        keys.keys[0] = {"Protocol", decoding::Protocols::name(d.protocol), true};
        keys.keys[1] = {"View", views[unsigned(d.view) < 3 ? unsigned(d.view) : 0], true};
        keys.keys[2] = {"Jump", data_.scopeHeld() ? "HOLD" : "HOLD only", data_.scopeHeld()};
        keys.keys[3] = decoderDetail_ ? ui::SoftKey{"Back", "Table", true} : ui::SoftKey{"Scope", "", true};
    } else { view_.softKeys(nullptr); return; }
    view_.softKeys(&keys);
}
// As imageKeys: F4 always (Back to the list, or out of it); F2 only in the preview (Full 1:1 or Fit);
// F3 with an image, not while the list is being cleared.
ui::SoftKeys ApplicationController::imageSoftKeys() const {
    ui::SoftKeys keys;
    const bool viewing = imageShowing_ || imageReader_.busy(), idle = !images_.clearing();
    if (viewing && idle) keys.keys[1] = imageFullSize_ ? ui::SoftKey{"Fit", "", true} : ui::SoftKey{"Full", "1:1", true};
    if (images_.count() && idle) keys.keys[2] = {"Delete", "", true};
    keys.keys[3] = {"Back", "", true};
    return keys;
}
void ApplicationController::meterHoldWord(char *output, size_t capacity) const {
    const auto &meter = data_.meter();
    if (meter.held()) { if (capacity) output[0] = 0; return; } // The manual or the DMM's own HOLD: "HOLD".
    meter.holdCycle().indicator(data_.nowUs(), output, capacity);
}
// While the display waits on DMA2D (RenderIdle, no LVGL): the newest frame not yet shown, looked
// at without being taken, has its columns reduced into the ColumnCache the projection then uses.
void ApplicationController::prepareColumns() {
    if (!rendering_ || state_.mode != MeasurementMode::Oscilloscope || panel_ != Panel::None || state_.settingsOpen || !uiReady_ ||
        data_.scopeHeld() || !view_.traceDrawn()) return;
    const auto &model = view_.waveform().model();
    if (!ahead_.valid()) {
        if (!data_.acquireScope(ahead_, false)) return;
    }
    if (model.hasFrame() && ahead_.view().metadata().sequence == model.metadata().sequence) {
        ahead_.release(); // Already shown: do not reduce it again or hold up publication of the next frame.
        return;
    }
    plot::applicationColumns().prepare(ahead_.view(), model.options(), model.viewport().width, model.viewport().height);
}
// Each entry to the Scope without a factory record asks once that the defaults be said; a notice already
// on show (a boot message, another action's) is never replaced: this one follows when it has gone.
void ApplicationController::nominalNotice(uint32_t now) {
    const bool scope = state_.mode == MeasurementMode::Oscilloscope;
    if (scope && !scopeShown_) nominalNoticeDue_ = data_.scopeNominal();
    if (!scope) nominalNoticeDue_ = false;
    scopeShown_ = scope;
    if (!nominalNoticeDue_ || !uiReady_ || view_.toastShowing()) return;
    view_.notify(Tooltip::Critical, "Factory calibration unavailable\nScope readings use approximate defaults", now);
    nominalNoticeDue_ = false;
}
void ApplicationController::displayData(uint32_t now) {
    refreshSoftKeys();
    nominalNotice(now);
    const bool rolled = uint32_t(now-fpsAt_) >= 1000; // FPS over each second, whatever is shown.
    if (rolled) {
        const uint32_t flushes=display_.flushes();
        fps_ = (flushes-fpsFlushes_)*1000/uint32_t(now-fpsAt_);
        fpsFlushes_=flushes; fpsAt_=now;
    }
    if (prefs_.showFps == 2) { // The sample rate in Scope, redrawn when it changes (a time/div at once).
        const auto rate = data_.scopeMetadata().samplesPerSecond;
        const double shown = state_.mode == MeasurementMode::Oscilloscope && rate.valid() ? rate.value() : 0;
        if (shown != counterRate_ || counterStale_) {
            char text[20];
            const bool ok = shown > 0 && plot::Format::quantity(shown, "S/s", text, sizeof text, 3);
            view_.counter(ok, ok ? text : "");
            counterRate_ = shown; counterStale_ = false;
        }
    } else if (rolled || counterStale_) { view_.fps(prefs_.showFps != 0, fps_); counterStale_ = false; }
    view_.generatorActive(data_.generator().enabled());
    if (ClockControl::changes() != clockChanges_) { clockChanges_ = ClockControl::changes(); clockMode_ = ClockControl::activeMode(); }
    view_.clockProfile(prefs_.clockIndicator != 0, clockMode_); // Redraws only on a change of its own.
    // The DAC stream halves the capture's maximum rate: said once as it starts (output on, PWM to DAC,
    // a Dedicated square below 1 kHz, a startup restore); the Scope's GEN note keeps ", ADC max /2".
    const bool captureHalved = data_.generatorLoadsBus();
    if (captureHalved && !captureHalvedNoticed_ && uiReady_ && panel_ != Panel::Bode)
        view_.notify(Tooltip::Advisory, "GEN on DAC: Scope max sample rate /2 while it runs", now);
    captureHalvedNoticed_ = captureHalved;
    if (state_.mode == MeasurementMode::Oscilloscope && panel_ != Panel::Bode) {
        view_.scopeTriggerState(data_.scopeRequest().trigger.mode, data_.scopeWaiting());
        view_.scopeCounters(data_.scopeCounters());
        view_.scopeAutoSetup(autoBusy());
        view_.scopeCalibrating(data_.zeroBusy());
    }
    if (state_.mode == MeasurementMode::Multimeter && !state_.settingsOpen && panel_ == Panel::None &&
        uint32_t(now - meterBarAt_) >= 8) {
        meterBarAt_ = now;
        const auto &meter = data_.meter();
        view_.multimeterBar(shownReading(meter, meterSelecting()), meter.status(data_.nowUs(),2000000), &meter.relative(), now);
    }
    if (state_.mode != MeasurementMode::Oscilloscope) readouts_.reset(data_); // Left the Scope: nothing kept.
    // The density map can take a new capture just before the ordinary 16 ms UI interval.
    // The display still presents completed buffers at its own refresh rate.
    const unsigned scopeInterval = view_.waveform().options().graded() ? 15 : 16;
    const unsigned interval = panel_ != Panel::None ? 40 : state_.mode == MeasurementMode::Oscilloscope ? scopeInterval
                              : state_.mode == MeasurementMode::Multimeter ? 10 : 40;
    if (uint32_t(now - dataAt_) < interval) return;
    // Bode (#72): the sweep's own service takes its records; no readouts, no decoder, no trace meanwhile.
    if (state_.settingsOpen || panel_==Panel::Images || panel_==Panel::Sessions || panel_==Panel::Zero || panel_==Panel::InterleaveMatch || panel_==Panel::GainMatch ||
        panel_==Panel::Bode) {
        readouts_.suspend(data_); // Not serviced while this lasts: no background lease is kept.
        dataAt_ = now;
        if (state_.mode == MeasurementMode::Oscilloscope) {
            if (panel_ != Panel::Bode) view_.captureStatus(data_.status(), data_.scopeHeld());
        } else if (state_.mode == MeasurementMode::Multimeter) {
            const auto &meter = data_.meter();
            char word[sizeof lastMeterHoldWord_];
            meterHoldWord(word, sizeof word);
            view_.multimeterStatus(shownReading(meter, meterSelecting()), meter.holding(), meter.status(data_.nowUs(), 2000000), meter.relative().active(), word);
        } else view_.generatorStatus(data_.generator().snapshot());
        return;
    }
    if (state_.mode == MeasurementMode::Oscilloscope) {
        auto options=view_.waveform().options();
        // Auto trials briefly release HOLD. Keep its view until Auto succeeds or returns to it.
        if (!(heldViewActive_ && autoBusy())) {
            if (heldViewActive_ && data_.scopeHoldReleases()!=heldViewRelease_) {
                options.restoreHeldView(heldView_);
                view_.waveform().configure(options); heldViewActive_=false; dataRefresh_=true;
            }
            followNextCapture(); // A timebase or channel change staged by keys, a menu or a host.
        }
        options=view_.waveform().options();
        // The next capture's settings do not change what a held record contains.
        if (!channelShown(options.spectrum.channel)) {
            options.spectrum.channel ^= 1;
            view_.waveform().configure(options);
        }
        if (options.cursors.voltage && !channelShown(options.cursors.channel) && channelShown(options.cursors.channel^1)) {
            auto cursors=options.cursors; cursors.channel^=1;
            view_.waveform().cursorLines(cursors); queuePlotSave(now);
        }
        followShownChannel();
        if (options.mode==plot::Mode::XY && (!channelShown(0) || !channelShown(1))) {
            options.mode=plot::Mode::Time; view_.waveform().configure(options);
            view_.notify(Tooltip::Advisory, "XY needs CH1 + CH2; Y-T active",now);
        }
        if (dataRefresh_) {
            auto next = data_.scopeMetadata();
            const auto &request = data_.scopeRequest();
            next.timebaseNs = request.timebaseNs; // The metadata keeps the running capture's until it restarts.
            const auto &model = view_.waveform().model();
            showTrigger(model.hasFrame() ? &model.metadata() : nullptr);
            view_.scopeContext(selectedChannel_,focus_,prefs_.panReverse != 0);
            view_.scopeSettings(next,selectedChannel_);
            syncCursorFocus();
            syncZoomFocus(); // X-Y, FFT or Roll now: Back to the card Zoom & Pan came from.
        }
        view_.scopeGenerator(data_.generator().snapshot(), captureHalved);
        {
            const auto &options = view_.waveform().options();
            char text[40];
            // A new clock profile or Scope entry (counterEpoch_) starts the mean again (S13). The profile as
            // read above on its last change (clockMode_), not an RCC read every pass.
            const auto counters = data_.scopeCounters();
            const unsigned source = options.counter.source ? options.counter.source - 1U : 0;
            const auto estimate = options.counter.estimate && view_.waveform().model().hasFrame()
                ? plot::CounterEstimate::of(counters.channels[source], readouts_.shown(), source) : plot::CounterEstimate::Value{};
            counterText_.update(counters, options.counter, text, sizeof text, clockMode_ | counterEpoch_ << 8, estimate);
            const bool room = options.mode == plot::Mode::Time; // X-Y and FFT label that corner.
            view_.waveform().counter(room ? text : "", view_.waveform().palette().channel(source));
        }
        if (!view_.traceDrawn()) { // A menu over the Scope, or Decoder only (plan §4.4).
            // No projection and no readouts without a trace; the decoder still takes the record.
            readouts_.suspend(data_); dataAt_=now;
            if (prefs_.scope.decoder.enabled) feedDecoder();
            showDecoder(now);
            if (view_.overlayActive()) return;
            if (dataRefresh_ || lastScopeStatus_ != data_.status() || lastScopeHold_ != data_.scopeHeld()) {
                view_.captureStatus(data_.status(), data_.scopeHeld());
                lastScopeStatus_ = data_.status(); lastScopeHold_ = data_.scopeHeld();
            }
            dataRefresh_ = false;
            return;
        }
        scope::FrameLease frame;
        // Nothing newer to show while Normal or Single waits: a moved view projects its record again.
        const auto &shown = view_.waveform().model();
        if (scopeFrame(frame) || (shown.needsFrame() && shown.hasFrame() && data_.acquireShownScope(frame, shown.metadata()))) {
            const auto sequence = frame.view().metadata().sequence;
            if (dataRefresh_ || lastScopeSequence_ != sequence || lastScopeHold_ != data_.scopeHeld() ||
                lastScopeStatus_ != data_.status() || view_.waveform().needsFrame()) {
                dataAt_ = now;
                showTrigger(&frame.view().metadata());
                view_.capture(frame.view(), data_.scopeHeld(), selectedChannel_, data_.status());
                lastScopeSequence_ = sequence;
                lastScopeHold_ = data_.scopeHeld();
                lastScopeStatus_ = data_.status();
            } else if (data_.scopeHeld())
                dataAt_ = now;
        } else if (dataRefresh_ || lastScopeStatus_ != data_.status()) {
            view_.captureStatus(data_.status(), data_.scopeHeld());
            lastScopeStatus_ = data_.status();
        }
        showDecoder(now); // After the trace took its record: the annotations and the table are of it.
        readouts_.service(data_, view_, plot::CounterEstimate::selection(state_.scope.measurements, view_.waveform().options().counter), now);
    } else if (state_.mode == MeasurementMode::Multimeter) {
        dataAt_ = now;
        const auto &meter = data_.meter();
        const auto status = meter.status(data_.nowUs(), 2000000);
        // The hold mode's word on the pill: its change is the capture's confirmation (WAIT -> HOLD,
        // A-WAIT -> A-HOLD, a new reading under A-HOLD), nothing over the reading or the keys, no
        // sound; a countdown second redraws only the pill.
        char word[sizeof lastMeterHoldWord_];
        meterHoldWord(word, sizeof word);
        const auto selecting = meterSelecting(); // Its sign over an empty reading (shownReading).
        const double charge = meter.charge().coulombs(); // The charge remains live when the main digits are held.
        if (dataRefresh_ || (selecting == dmm::Function::Unknown && lastMeterSequence_ != meter.displayed().sequence) || lastMeterSelecting_ != selecting ||
            lastMeterHold_ != meter.holding() || lastMeterStatus_ != status || lastMeterCharge_ != charge) {
            const ui::MeterContext context{&meter.statistics(), &meter.history(), &meter.relative(), prefs_.meter.display,
                                          bool(prefs_.meter.statistics), prefs_.meter.shownFace(), prefs_.meter.shownPalette(),
                                          &prefs_.meter.shownAppearance(), word, bool(prefs_.meter.bar),
                                          data_.meterLogger().active() || data_.meterSession().recording(),
                                          selecting, &meter.charge(), prefs_.meter.barUnit};
            view_.multimeter(shownReading(meter, selecting), meter.holding(), status, context);
            lastMeterSelecting_ = selecting;
            lastMeterSequence_ = meter.displayed().sequence;
            lastMeterStatus_ = status;
            lastMeterHold_ = meter.holding();
            lastMeterCharge_ = charge;
        } else if (std::strcmp(word, lastMeterHoldWord_))
            view_.multimeterStatus(shownReading(meter, selecting), meter.holding(), status,
                                   meter.relative().matches(shownReading(meter, selecting)), word);
        std::snprintf(lastMeterHoldWord_, sizeof lastMeterHoldWord_, "%s", word);
    } else {
        dataAt_ = now;
        const auto output = data_.generator().snapshot();
        if (dataRefresh_ || output.enabled != lastGenerator_.enabled ||
            output.status != lastGenerator_.status) {
            view_.generator(output);
            lastGenerator_ = output;
        }
    }
    dataRefresh_ = false;
}
} // namespace openosc
