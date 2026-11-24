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
alignas(8) __attribute__((section(".scpi_measure"))) unsigned char measureStorage[sizeof(scope::RecordReadouts)];
scope::RecordReadouts *measureJob = nullptr; // Built on first use.
const void *measureOwner = nullptr;          // The engine whose WAV:MEAS? RECord runs it,
uint32_t measureUntil = 0;                   // its bound (an owner not polled 1 s past it loses the job).
// Another session's job still running (taken over 1 s past its bound); a query waits to ownerUntil_, then -230.
bool otherOwner(const void *self, uint32_t now) {
    if (!measureOwner || measureOwner == self) return false;
    if (int32_t(now - measureUntil) < 1000) return true;
    measureJob->cancel(); measureOwner = nullptr; return false; // Its owner's stepMeasure sees the job gone (-230).
}
constexpr uint32_t MeasureSlice = 32768;     // Record positions a poll (ReadoutScheduler::SlicePoints).
}
// The display's Screen readouts (ScreenReadouts) over START..STOP as the plot's columns would see it.
bool Engine::measure(const scope::FrameView &frame, uint32_t first, uint32_t end, unsigned c, char *out, size_t size) {
    using scope::Measurement;
    const auto &m = frame.metadata();
    const uint32_t n = end - first, width = plot::PlotFrame::PlotWidth;
    plot::ScreenSeries series; // ~1.4 KiB on the dispatcher's stack.
    series.aggregated = n > width; series.first = first; series.last = end; series.sequence = m.sequence;
    series.minimum[c] = 127; series.maximum[c] = -128;
    scope::BandEstimate band;
    bool clipped = false;
    const auto add = [&](int16_t low, int16_t high) {
        series.minimum[c] = std::min(series.minimum[c], low); series.maximum[c] = std::max(series.maximum[c], high);
        band.add(low, high); clipped |= low <= -128 || high >= 127;
    };
    if (series.aggregated) {
        series.samplesPerSecond = m.samplesPerSecond.value() * width / n;
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t a = first + uint32_t(uint64_t(n) * x / width), b = first + uint32_t(uint64_t(n) * (x + 1) / width);
            scope::ColumnStats s;
            if (frame.cursor(scope::Channel(c), a).column(b - a, s) != b - a) return false;
            series.codes[c][x] = int8_t(s.first); series.spans[c][x] = uint8_t(s.high - s.low);
            add(s.low, s.high);
        }
        series.count[c] = width;
    } else {
        series.samplesPerSecond = m.samplesPerSecond.value();
        for (uint32_t i = first; i < end; ++i) {
            int16_t code;
            if (!frame.signedCode(scope::Channel(c), i, code)) return false;
            series.codes[c][series.count[c]++] = int8_t(code);
            add(code, code);
        }
    }
    series.valid[c] = series.count[c] > 0;
    series.band[c] = band.band();
    const auto result = plot::ScreenReadouts::calculate(series, m, selection(c, scope::ReadoutBasis::Screen), now_);
    return result.computed[c] && measured(result.values[c], clipped, c, out, size);
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
bool Engine::point(const scope::FrameView &frame, const Segment &s, uint32_t index, int16_t &low, int16_t &high, uint32_t &scanned) const {
    const auto channel = scope::Channel(s.channel);
    const uint32_t at = s.first + index * s.stride;
    if (!peakStream_) { ++scanned; return frame.signedCode(channel, at, low); }
    const uint32_t end = at + s.stride < s.end ? at + s.stride : s.end;
    low = 127; high = -128;
    for (uint32_t i = at; i < end; ++i) {
        int16_t code;
        if (!frame.signedCode(channel, i, code)) return false;
        if (code < low) low = code;
        if (code > high) high = code;
    }
    scanned += end - at;
    return end > at;
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
// One slice of WAV:MEAS? RECord on the leased record; a lost lease or another record is -230.
void Engine::stepMeasure() {
    if (measureOwner != this) { measuring_ = false; error(-230, true); return; } // Taken over: this session was not polled.
    const auto *f = frame(); // Keeps frameAt_: the measured record stays leased for the reads after it.
    if (!f || f->metadata().sequence != measureSequence_ || int32_t(now_ - deadline_) >= 0) { endMeasure(); error(-230, true); return; }
    const bool done = measureJob->step(*f, MeasureSlice, now_);
    if (measureJob->running()) return;
    const auto &r = measureJob->result();
    const unsigned c = waveChannel_;
    endMeasure();
    char text[sizeof response_ - 2];
    // Clip here: the signal's band at a rail (Measurements::clipMark), not a lone point as on the Screen basis.
    if (!done || !r.computed[c] || !measured(r.values[c], r.values[c].clipMark(scope::Measurement::PeakToPeak) != nullptr, c, text, sizeof text)) error(-230, true);
    else reply("%s", text);
}
void Engine::endMeasure() {
    if (measureOwner == this) { measureJob->cancel(); measureOwner = nullptr; }
    measuring_ = false;
}
// The sole preserved frame is briefly unavailable during its DMA copy: the query waits up to 1 s.
void Engine::waitFrame(const char *query) {
    if (!waitingWaveform_) { waitingWaveform_ = true; pendingWave_ = query; deadline_ = now_ + 1000; return; }
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
        if (!c.query) { error(-109); return true; }
        if (!waitingWaveform_) { // A replay carries no argument: the first call's basis stays.
            ownerUntil_ = 0;
            if (!*c.argument || Header::word(c.argument, "SCReen")) measureRecord_ = false;
            else if (Header::word(c.argument, "RECord")) measureRecord_ = true;
            else { error(-224, true); return true; }
        }
        if (measureRecord_ && otherOwner(this, now_)) { // Waited for, its lease kept (a release could leave no record), to the first job's bound.
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
        if (measureRecord_) { // Every point of the record, START/STOP aside.
            if (!measureJob) measureJob = new (measureStorage) scope::RecordReadouts();
            measureJob->start(*f, selection(waveChannel_, scope::ReadoutBasis::Record));
            measureOwner = this; measuring_ = true; measureSequence_ = m.sequence; deadline_ = measureUntil = now_ + MeasureMs;
            stepMeasure(); return true;
        }
        if (!s.range || s.first >= s.end) { error(-222, true); return true; }
        char text[sizeof response_ - 2];
        if (!measure(*f, s.first, s.end, waveChannel_, text, sizeof text)) error(-230, true); else reply("%s", text);
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
            segmentAt_ = 0; pointAt_ = 0; binarySent_ = 0; maxPending_ = false;
            binaryBytes_ = s.sent * (peakStream_ ? 2 : 1);
            char count[16]; std::snprintf(count, sizeof count, "%lu", (unsigned long)binaryBytes_);
            reply("#%u%s", unsigned(std::strlen(count)), count);
            binary_ = true;
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
        // A new record each query in RUN (the backend waits for one newer than the last); HOLD repeats its own.
        const auto *f = frame(!waitingWaveform_);
        if (!f && waitingWaveform_ && int32_t(now_ - deadline_) >= 0) { backend_.repeatFrame(); f = frame(); } // None newer in 1 s.
        if (!f) { waitFrame(envelope ? "ENV" : "FRAM"); return true; }
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
        segmentAt_ = 0; pointAt_ = 0; binarySent_ = 0; maxPending_ = false;
        binaryBytes_ = segmentCount_ * s.sent * (peakStream_ ? 2 : 1);
        reply("#9%09lu%s", (unsigned long)(binaryBytes_ + uint32_t(used)), text);
        binary_ = true;
    } else return false;
    return true;
}
} // namespace openosc::scpi
