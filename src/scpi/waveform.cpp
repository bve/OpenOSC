#include "openosc/scpi/engine.hpp"
#include "openosc/data/decimal.hpp"
#include "openosc/acquisition/readouts.hpp"
#include "openosc/acquisition/record_depths.hpp"
#include "openosc/ui/scope/plot_frame.hpp"
#include "openosc/ui/scope/screen_readouts.hpp"
#include <algorithm>
#include <new>
#include <cstdio>
#include <cstring>

namespace openosc::scpi {
namespace {
// START..STOP (1-based) as at most WAV:POIN points or buckets of stride samples; range: within the record.
struct Span { uint32_t first, end, stride, sent; bool range; };
Span span(uint32_t start, uint32_t stop, uint32_t points, uint32_t samples) {
    const uint32_t end = stop ? stop : samples, first = start - 1;
    const bool range = start <= end && end <= samples;
    const uint32_t stride = points && range && end - first > points ? (end - first + points - 1) / points : 1;
    return {first, end, stride, range ? (end - first + stride - 1) / stride : 0, range};
}
// f, Amp (band), Vpp and Duty, so every pass runs; only channel c.
scope::ReadoutSelection selection(unsigned c, scope::ReadoutBasis basis) {
    using scope::Measurement;
    scope::ReadoutSelection s;
    s.basis = basis;
    for (auto &v : s.values[1 - c]) v = unsigned(Measurement::Off);
    s.values[c][0] = unsigned(Measurement::Frequency); s.values[c][1] = unsigned(Measurement::Amplitude);
    s.values[c][2] = unsigned(Measurement::PeakToPeak); s.values[c][3] = unsigned(Measurement::Duty);
    return s;
}
// WAV:MEAS? SCReen: START..STOP reduced to the plot's columns as the display's projection reduces its
// window (each column's first code, its envelope's span, the extremes and the robust band), then the
// Screen basis' own ScreenReadouts. It is the display's reading only when that window is the viewport's.
struct ScreenJob {
    plot::ScreenSeries series;
    scope::BandEstimate band;
    uint32_t first = 0, end = 0, column = 0;
    unsigned channel = 0;
    bool clipped = false;
    void add(int16_t low, int16_t high) {
        const unsigned c = channel;
        series.minimum[c] = std::min(series.minimum[c], low); series.maximum[c] = std::max(series.maximum[c], high);
        band.add(low, high); clipped |= low <= -128 || high >= 127;
    }
    // At most budget record points (and one column); true once every column is in.
    bool step(const scope::FrameView &frame, uint32_t budget, bool &failed) {
        const unsigned c = channel;
        const uint32_t n = end - first, width = plot::PlotFrame::PlotWidth;
        if (!series.aggregated) { // At most a column's worth of points: each as it is.
            for (uint32_t i = first; i < end; ++i) {
                int16_t code;
                if (!frame.signedCode(scope::Channel(c), i, code)) { failed = true; return true; }
                series.codes[c][series.count[c]++] = int8_t(code);
                add(code, code);
            }
            return true;
        }
        for (uint32_t used = 0; column < width && used < budget; ++column) {
            const uint32_t a = first + uint32_t(uint64_t(n) * column / width), b = first + uint32_t(uint64_t(n) * (column + 1) / width);
            scope::ColumnStats s;
            auto cursor = frame.cursor(scope::Channel(c), a);
            if (cursor.column(b - a, s) != b - a) { failed = true; return true; }
            series.codes[c][column] = int8_t(s.first); series.spans[c][column] = uint8_t(s.high - s.low);
            add(s.low, s.high);
            used += b - a;
        }
        series.count[c] = column;
        return column == width;
    }
};
// One job for every session in ITCM: the record's readouts or the Screen series, never both.
constexpr size_t MeasureBytes = std::max(sizeof(scope::RecordReadouts), sizeof(ScreenJob));
alignas(8) __attribute__((section(".scpi_measure"))) unsigned char measureStorage[MeasureBytes];
static_assert(alignof(scope::RecordReadouts) <= 8 && alignof(ScreenJob) <= 8);
scope::RecordReadouts *recordJob = nullptr; // The running job: one of these two, built in place at its start.
ScreenJob *screenJob = nullptr;
const void *measureOwner = nullptr;          // The engine whose WAV:MEAS? runs it,
uint32_t measureUntil = 0;                   // its bound (an owner not polled 1 s past it loses the job).
void dropJob() { if (recordJob) recordJob->cancel(); recordJob = nullptr; screenJob = nullptr; measureOwner = nullptr; }
// Another session's job still running (taken over 1 s past its bound); a query waits to ownerUntil_, then -230.
bool otherOwner(const void *self, uint32_t now) {
    if (!measureOwner || measureOwner == self) return false;
    if (int32_t(now - measureUntil) < 1000) return true;
    dropJob(); return false; // Its owner's stepMeasure sees the job gone (-230).
}
constexpr uint32_t MeasureSlice = 32768;     // Record positions a poll (ReadoutScheduler::SlicePoints).
}
// The 15 values (8 digits, NAN: unknown; FC: a settled count), the clip flag and the f source.
bool Engine::measured(const scope::Measurements &values, bool clipped, unsigned c, char *out, size_t size) {
    using scope::Measurement;
    scope::CounterReading counter;
    const bool counted = backend_.counter(c, counter) && counter.settled() && counter.hertz > 0;
    size_t used = 0;
    for (unsigned i = 0; i < scope::Measurements::Count; ++i) {
        double v;
        const bool known = Measurement(i) == Measurement::CounterFrequency ? (v = counter.hertz, counted) : values.value(Measurement(i), v);
        char text[32];
        if (!known || !Decimal::number(v, text, sizeof text, 8)) std::strcpy(text, "NAN");
        const int k = std::snprintf(out + used, size - used, "%s,", text);
        if (k < 0 || size_t(k) >= size - used) return false;
        used += size_t(k);
    }
    static constexpr const char *Sources[] = {"NONE", "EDGE", "SPEC", "CNT"};
    const int k = std::snprintf(out + used, size - used, "%u,%s", clipped ? 1U : 0U, Sources[unsigned(values.frequencySource())]);
    return k > 0 && size_t(k) < size - used;
}
// The pinned record went while its block was sent: zeros complete the announced length, the LF ends
// it, -230 is queued once and the lease goes back (the session stays).
void Engine::lostBlock() {
    if (binarySent_ < binaryBytes_) { padding_ += binaryBytes_ - binarySent_; binarySent_ = binaryBytes_; errors_.push(-230); }
    waveAt_ = waveEnd_ = 0;
    backend_.releaseFrame();
}
void Engine::endWave() {
    if (waveStage_) releaseStage(this);
    waveStage_ = nullptr; waveAt_ = waveEnd_ = 0;
}
// Foreground only (poll, refill): the next min/max pairs of a peak block, once read() emptied the
// stage. A fill reduces at most WaveSliceSamples points with the
// frame's sequential column reader (the codes, glitch resolution and correction of signedCode).
void Engine::fillWave() {
    if (!waveMore()) return;
    const size_t ready = waveEnd_ - waveAt_;
    if (ready > StageBytes / 2) return;
    const auto *f = backend_.frame(false);
    if (!pinned(f)) { lostBlock(); return; }
    if (ready && waveAt_) std::memmove(waveStage_, waveStage_ + waveAt_, ready);
    waveAt_ = 0;
    waveEnd_ = uint16_t(ready);
    for (uint32_t scanned = 0; size_t(waveEnd_) + 2 <= StageBytes && segmentAt_ < segmentCount_ && scanned < WaveSliceSamples;) {
        const auto &segment = segments_[segmentAt_];
        if (pointAt_ == segment.count) { ++segmentAt_; pointAt_ = 0; continue; }
        const uint32_t at = segment.first + pointAt_ * segment.stride, end = std::min(at + segment.stride, segment.end);
        if (end <= at) { lostBlock(); return; }
        if (end - at > WaveSliceSamples - scanned) break;
        int16_t low = INT8_MAX, high = INT8_MIN;
        auto cursor = f->cursor(scope::Channel(segment.channel), at);
        for (uint32_t left = end - at; left && (low > INT8_MIN || high < INT8_MAX);) {
            // Once both ADC rails are present, later samples cannot change either extreme.
            constexpr uint32_t Block = 256;
            const uint32_t count = std::min(left, Block);
            scope::ColumnStats stats;
            if (cursor.column(count, stats) != count) { lostBlock(); return; }
            low = std::min(low, stats.low);
            high = std::max(high, stats.high);
            left -= count;
            scanned += count;
        }
        waveStage_[waveEnd_++] = uint8_t(low + 128); waveStage_[waveEnd_++] = uint8_t(high + 128);
        ++pointAt_;
    }
}
// A block from the leased record (segments_, binaryBytes_ and peakStream_ set): the record is
// pinned and a peak block claims its stage. False: no stage free (-200 queued and NAN sent).
bool Engine::prepareBlock(const scope::FrameMetadata &m) {
    pin(m);
    segmentAt_ = 0; pointAt_ = 0; binarySent_ = 0;
    endWave();
    if (peakStream_ && !(waveStage_ = claimStage(this))) { error(-200, true); return false; } // Both stages stream.
    return true;
}
// After the header's reply (which ends any earlier output): the block follows it, a peak block with
// its first pairs staged.
void Engine::openBlock() {
    binary_ = true;
    fillWave();
}
// The whole record (WAV:POIN 0, RAW) unless `exact` or WAV:POIN n: then exactly the block sent (type 1: peak).
bool Engine::preamble(const scope::FrameMetadata &m, unsigned channel, bool exact, char *out, size_t size, uint32_t envelope) const {
    const auto &ch = m.channels[channel];
    const auto s = envelope ? span(1, 0, envelope, m.samples) : span(waveStart_, waveStop_, wavePoints_, m.samples);
    const double interval = double(m.samplesPerSecond.denominator) / m.samplesPerSecond.numerator;
    const double origin0 = m.triggered ? -interval * m.triggerIndex : 0.;
    const bool thinned = exact || wavePoints_ != 0 || envelope;
    char increment[40], origin[40], gain[40], zero[40];
    if (!Decimal::number(interval * s.stride,increment,sizeof increment) ||
        !Decimal::number(thinned ? origin0 + interval * s.first : origin0,origin,sizeof origin) ||
        !Decimal::number(ch.voltsPerCode * ch.probeNumerator / ch.probeDenominator,gain,sizeof gain) ||
        !Decimal::number(ch.zeroCode,zero,sizeof zero)) return false;
    const int n = std::snprintf(out, size, "0,%u,%lu,1,%s,%s,0,%s,%s,128", envelope || (wavePeak_ && wavePoints_) ? 1U : 0U,
                                (unsigned long)(thinned ? s.sent : m.samples), increment, origin, gain, zero);
    return n > 0 && size_t(n) < size;
}
// One slice of WAV:MEAS? on the pinned record (at most MeasureSlice points a poll, either basis); a
// lost lease, another record, a job taken over or its bound passed is -230.
void Engine::stepMeasure() {
    if (measureOwner != this) { measuring_ = false; error(-230, true); return; } // Taken over: this session was not polled.
    const auto *f = frame(); // Keeps frameAt_: the measured record stays leased for the reads after it.
    if (!pinned(f) || int32_t(now_ - deadline_) >= 0) { endMeasure(); error(-230, true); return; }
    const unsigned c = waveChannel_;
    char text[sizeof response_ - 2];
    if (recordJob) {
        const bool done = recordJob->step(*f, MeasureSlice, now_);
        if (recordJob->running()) return;
        const auto &r = recordJob->result();
        endMeasure();
        // Clip here: the signal's band at a rail (Measurements::clipMark), not a lone point as on the Screen basis.
        if (!done || !r.computed[c] || !measured(r.values[c], r.values[c].clipMark(scope::Measurement::PeakToPeak) != nullptr, c, text, sizeof text)) error(-230, true);
        else reply("%s", text);
        return;
    }
    bool failed = false;
    if (!screenJob->step(*f, MeasureSlice, failed) && !failed) return;
    auto &job = *screenJob;
    endMeasure();
    if (failed) { error(-230, true); return; }
    job.series.valid[c] = job.series.count[c] > 0;
    job.series.band[c] = job.band.band();
    const auto result = plot::ScreenReadouts::calculate(job.series, f->metadata(), selection(c, scope::ReadoutBasis::Screen), now_);
    if (!result.computed[c] || !measured(result.values[c], job.clipped, c, text, sizeof text)) error(-230, true);
    else reply("%s", text);
}
void Engine::endMeasure() {
    if (measureOwner == this) dropJob();
    measuring_ = false;
}
// A legacy query gives the preserved record's DMA copy 1 s. A fresh-record query in RUN
// also allows one acquisition under the current plan, including the first query after a change.
void Engine::waitFrame(const char *query, bool fresh) {
    if (!waitingWaveform_) {
        uint64_t recordMs = 0;
        if (fresh && !backend_.held()) {
            const auto *plan = backend_.configuration();
            if (plan && plan->samplesPerSecond.valid()) {
                const auto rate = plan->samplesPerSecond;
                recordMs = (1000ULL * plan->samples * rate.denominator + rate.numerator - 1) / rate.numerator;
            } else recordMs = backend_.scopeRequest().timebaseNs / 100000; // Ten divisions in ms.
        }
        constexpr uint32_t MaximumRecordWaitMs = 200000; // Ten divisions at the slowest timebase.
        waitingWaveform_ = true; pendingWave_ = query;
        deadline_ = now_ + 1000 + uint32_t(std::min<uint64_t>(recordMs, MaximumRecordWaitMs));
        return;
    }
    if (int32_t(now_ - deadline_) < 0) return;
    waitingWaveform_ = false; error(-230, true);
}
bool Engine::waveformCommand(const Command &c) {
    const auto match = [&](const char *pattern) { return Header::matches(c.header, pattern); };
    if (match("WAVeform:SOURce")) {
        if (c.query) reply("CHAN%u", waveChannel_ + 1);
        else { unsigned ch = 0; if (!Header::matches(c.argument, "CHANnel#", &ch) || ch < 1 || ch > 2) error(-224); else waveChannel_ = ch - 1; }
    } else if (match("WAVeform:FORMat") || match("WAVeform:MODE")) {
        const auto *value = match("WAVeform:FORMat") ? "BYTE" : "RAW";
        if (c.query) reply("%s", value); else if (!Header::word(c.argument, value)) error(-224);
    } else if (match("WAVeform:STARt") || match("WAVeform:STOP")) {
        auto &value = match("WAVeform:STARt") ? waveStart_ : waveStop_;
        if (c.query) { // Unset, the record's length: it waits for the frame as the other frame queries do.
            if (value) reply("%lu", (unsigned long)value);
            else if (const auto *f = frame()) { waitingWaveform_ = false; reply("%lu", (unsigned long)f->metadata().samples); }
            else waitFrame(match("WAVeform:STARt") ? "STAR" : "STOP");
        }
        else { double number; if (argument(c, number)) { if (number < 1 || number > scope::RecordDepth::Maximum || number != uint32_t(number)) error(-222); else value = uint32_t(number); } }
    } else if (match("WAVeform:POINts")) { // 0: every point; else at most this many, every Nth of START..STOP.
        if (c.query) reply("%lu", (unsigned long)wavePoints_);
        else { double number; if (argument(c, number)) { if (number < 0 || (number && number < 100) || number > scope::RecordDepth::Maximum || number != uint32_t(number)) error(-222); else wavePoints_ = uint32_t(number); } }
    } else if (match("WAVeform:POINts:MODE")) { // STRide: every Nth point; PEAK: a min and a max byte a bucket.
        if (c.query) reply("%s", wavePeak_ ? "PEAK" : "STR");
        else if (Header::word(c.argument, "PEAK")) wavePeak_ = true;
        else if (Header::word(c.argument, "STRide")) wavePeak_ = false;
        else error(-224);
    } else if (match("WAVeform:SNAPshot")) {
        if (c.query || *c.argument) error(-108,c.query);
        else { backend_.releaseFrame(); preambleChannels_ = 0; }
    } else if (match("WAVeform:KEY")) { // The leased record's full key: matched with DECode:RESult?.
        char key[96];
        if (!c.query) error(-109);
        else if (!frame()) waitFrame("KEY");
        else {
            waitingWaveform_ = false;
            if (!backend_.frameKey(key, sizeof key)) error(-230, true); else reply("%s", key);
        }
    } else if (match("WAVeform:MEASure")) {
        // [SCReen|RECord]: the 15 readouts in Measurement order (NAN: unknown), ADC clip 0|1, f source NONE|EDGE|SPEC|CNT.
        // The leased record, as WAV:SEQ? names it (FRAM? then MEAS? read one record; WAV:SNAP takes a newer):
        // SCReen is the Screen basis' algorithm over START..STOP, RECord the Full record basis over all of it.
        if (!c.query) { error(-109); return true; }
        if (!waitingWaveform_) { // A replay carries no argument: the first call's basis stays.
            ownerUntil_ = 0;
            if (!*c.argument || Header::word(c.argument, "SCReen")) measureRecord_ = false;
            else if (Header::word(c.argument, "RECord")) measureRecord_ = true;
            else { error(-224, true); return true; }
        }
        if (otherOwner(this, now_)) { // Either basis (one job for every session): waited for, its lease kept, to the first job's bound.
            if (!ownerUntil_) ownerUntil_ = (measureUntil + 1000) | 1;
            if (int32_t(now_ - ownerUntil_) >= 0) { waitingWaveform_ = false; error(-230, true); return true; }
            waitingWaveform_ = true; pendingWave_ = "MEAS"; deadline_ = now_ + 1000; return true; // A frame's wait from here: 1 s.
        }
        const auto *f = frame();
        if (!f) { waitFrame("MEAS"); return true; }
        waitingWaveform_ = false;
        const auto &m = f->metadata();
        const auto s = span(waveStart_, waveStop_, 0, m.samples);
        if (!m.channels[waveChannel_].enabled || !m.channels[waveChannel_].calibrated || !m.samplesPerSecond.valid()) { error(-230, true); return true; }
        if (!measureRecord_ && (!s.range || s.first >= s.end)) { error(-222, true); return true; }
        dropJob(); // Free, or this session's own (otherOwner): built anew in place.
        if (measureRecord_) { // Every point of the record, START/STOP aside.
            recordJob = new (measureStorage) scope::RecordReadouts();
            recordJob->start(*f, selection(waveChannel_, scope::ReadoutBasis::Record));
        } else {
            screenJob = new (measureStorage) ScreenJob();
            auto &job = *screenJob;
            job.first = s.first; job.end = s.end; job.channel = waveChannel_;
            job.series.aggregated = s.end - s.first > plot::PlotFrame::PlotWidth;
            job.series.first = s.first; job.series.last = s.end; job.series.sequence = m.sequence;
            job.series.minimum[waveChannel_] = 127; job.series.maximum[waveChannel_] = -128;
            job.series.samplesPerSecond = m.samplesPerSecond.value() * (job.series.aggregated ? double(plot::PlotFrame::PlotWidth) / (s.end - s.first) : 1.);
        }
        measureOwner = this; measuring_ = true; pin(m); deadline_ = measureUntil = now_ + MeasureMs;
        stepMeasure();
    } else if (match("WAVeform:SEQuence") || match("WAVeform:TIMing")) {
        const auto *f = frame();
        if (!c.query) error(-109); else if (!f) waitFrame(match("WAVeform:TIMing") ? "TIM" : "SEQ");
        else { waitingWaveform_ = false; reply("%lu", (unsigned long)(match("WAVeform:TIMing") ? f->metadata().timingVerified : f->metadata().sequence)); }
    } else if (match("WAVeform:EVENt")) { // Diagnostic: the comparator event, in points from the trigger point.
        const auto *f = frame();
        if (!c.query) error(-109); else if (!f) waitFrame("EVEN");
        else if (!f->metadata().eventKnown) { waitingWaveform_ = false; error(-230, true); }
        else { waitingWaveform_ = false; reply("%ld", long(f->metadata().eventOffset)); }
    } else if (match("WAVeform:PREamble") || match("WAVeform:DATA")) {
        if (!c.query) { error(-109); return true; }
        const bool preamble = match("WAVeform:PREamble");
        const bool repeated = preamble && (preambleChannels_ & (1U << waveChannel_));
        const auto *f = frame(repeated);
        if (repeated) preambleChannels_ = 0;
        if (!f) {
            // The sole preserved frame is briefly unavailable during its DMA copy.
            // Wait in the foreground dispatcher; USB, capture and local UI continue.
            waitFrame(preamble ? "PRE" : "DATA");
            return true;
        }
        waitingWaveform_ = false;
        if (!f->metadata().channels[waveChannel_].enabled) { error(-230, true); return true; }
        const auto &m = f->metadata();
        if (!m.channels[waveChannel_].calibrated || !m.samplesPerSecond.valid()) { error(-230, true); return true; }
        // WAV:POIN n: PRE describes exactly DATA's block and an out-of-record range fails both (-222).
        const auto s = span(waveStart_, waveStop_, wavePoints_, m.samples);
        if ((!preamble || wavePoints_) && !s.range) { error(-222, true); return true; }
        if (preamble) {
            preambleChannels_ |= 1U << waveChannel_;
            char text[200];
            if (!this->preamble(m, waveChannel_, false, text, sizeof text)) error(-230, true); else reply("%s", text);
        } else {
            peakStream_ = wavePeak_ && wavePoints_;
            segments_[0] = {s.first, s.stride, s.sent, s.end, waveChannel_}; segmentCount_ = 1;
            binaryBytes_ = s.sent * (peakStream_ ? 2 : 1);
            if (!prepareBlock(m)) return true;
            char count[16]; std::snprintf(count, sizeof count, "%lu", (unsigned long)binaryBytes_);
            reply("#%u%s", unsigned(std::strlen(count)), count);
            openBlock();
        }
    } else if (match("WAVeform:FRAMe") || match("WAVeform:ENVelope")) {
        // One block: "seq,channels\n", "CHANn,<exact preamble>\n" per displayed channel, then their points.
        // ENV? [buckets]: the whole record as min/max pairs (type 1), START/STOP/POIN aside; default EnvelopeBuckets.
        if (!c.query) { error(-109); return true; }
        const bool envelope = match("WAVeform:ENVelope");
        if (envelope && !waitingWaveform_) { // A replay carries no argument: the first call's count stays.
            double number = EnvelopeBuckets;
            if (*c.argument && !argument(c, number)) return true;
            if (number < 100 || number > scope::RecordDepth::Maximum || number != uint32_t(number)) { error(-222, true); return true; }
            waveEnvelope_ = uint32_t(number);
        }
        // RUN waits for a new record. HOLD and completed Single repeat their own.
        if (!waitingWaveform_) preambleChannels_ = 0; // The lease moves: PRE?'s no longer applies.
        const auto *f = frame(!waitingWaveform_);
        if (!f) { waitFrame(envelope ? "ENV" : "FRAM", true); return true; }
        waitingWaveform_ = false;
        const auto &m = f->metadata();
        const auto s = envelope ? span(1, 0, waveEnvelope_, m.samples) : span(waveStart_, waveStop_, wavePoints_, m.samples);
        if (!s.range) { error(-222, true); return true; }
        char text[232]; int used = 0; unsigned shown = 0;
        for (unsigned ch = 0; ch < 2; ++ch) shown += m.channels[ch].enabled && m.channels[ch].calibrated;
        if (!shown || !m.samplesPerSecond.valid()) { error(-230, true); return true; }
        used = std::snprintf(text, sizeof text, "%lu,%u\n", (unsigned long)m.sequence, shown);
        peakStream_ = envelope || (wavePeak_ && wavePoints_); segmentCount_ = 0;
        for (unsigned ch = 0; ch < 2; ++ch) {
            if (!m.channels[ch].enabled || !m.channels[ch].calibrated) continue;
            char line[200];
            if (!this->preamble(m, ch, true, line, sizeof line, envelope ? waveEnvelope_ : 0)) { error(-230, true); return true; }
            const int n = std::snprintf(text + used, sizeof text - size_t(used), "CHAN%u,%s\n", ch + 1, line);
            if (n < 0 || size_t(n) >= sizeof text - size_t(used)) { error(-230, true); return true; }
            used += n;
            segments_[segmentCount_++] = {s.first, s.stride, s.sent, s.end, ch};
            preambleChannels_ |= 1U << ch;
        }
        binaryBytes_ = segmentCount_ * s.sent * (peakStream_ ? 2 : 1);
        if (!prepareBlock(m)) return true;
        reply("#9%09lu%s", (unsigned long)(binaryBytes_ + uint32_t(used)), text);
        openBlock();
    } else return false;
    return true;
}
} // namespace openosc::scpi
