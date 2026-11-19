#include "openosc/scpi/engine.hpp"
#include "openosc/data/decimal.hpp"
#include "openosc/acquisition/record_depths.hpp"
#include <cstdio>
#include <cstring>

namespace openosc::scpi {
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
        if (c.query) { if (value) reply("%lu", (unsigned long)value); else { const auto *f = frame(); if (f) reply("%lu", (unsigned long)f->metadata().samples); else error(-230, true); } }
        else { double number; if (argument(c, number)) { if (number < 1 || number > scope::RecordDepth::Maximum || number != uint32_t(number)) error(-222); else value = uint32_t(number); } }
    } else if (match("WAVeform:POINts")) { // 0: every point; else at most this many, every Nth of START..STOP.
        if (c.query) reply("%lu", (unsigned long)wavePoints_);
        else { double number; if (argument(c, number)) { if (number < 0 || (number && number < 100) || number > scope::RecordDepth::Maximum || number != uint32_t(number)) error(-222); else wavePoints_ = uint32_t(number); } }
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
        const auto &m = f->metadata(); const auto &ch = m.channels[waveChannel_];
        if (!ch.calibrated || !m.samplesPerSecond.valid()) { error(-230, true); return true; }
        const uint32_t end = waveStop_ ? waveStop_ : m.samples, first = waveStart_ - 1;
        const uint32_t stride = wavePoints_ && end > first && end - first > wavePoints_ ? (end - first + wavePoints_ - 1) / wavePoints_ : 1;
        if (preamble) {
            preambleChannels_ |= 1U << waveChannel_;
            const double interval = double(m.samplesPerSecond.denominator) / m.samplesPerSecond.numerator;
            const double origin0 = m.triggered ? -interval * m.triggerIndex : 0.;
            // Thinned (WAV:POIN): the points, their spacing and the first one's time as sent.
            const bool thinned = wavePoints_ && end <= m.samples && first < end;
            const uint32_t points = thinned ? (end - first + stride - 1) / stride : m.samples;
            char increment[40], origin[40], gain[40], zero[40];
            if (!Decimal::number(interval * (thinned ? stride : 1),increment,sizeof increment) ||
                !Decimal::number(thinned ? origin0 + interval * first : origin0,origin,sizeof origin) ||
                !Decimal::number(ch.voltsPerCode * ch.probeNumerator / ch.probeDenominator,gain,sizeof gain) ||
                !Decimal::number(ch.zeroCode,zero,sizeof zero)) error(-230,true);
            else reply("0,0,%lu,1,%s,%s,0,%s,%s,128", (unsigned long)points,increment,origin,gain,zero);
        } else {
            if (waveStart_ > end || end > m.samples) { error(-222, true); return true; }
            binaryStart_ = first; binaryStride_ = stride; binaryCount_ = (end - first + stride - 1) / stride; binaryAt_ = 0;
            char count[16]; std::snprintf(count, sizeof count, "%lu", (unsigned long)binaryCount_);
            reply("#%u%s", unsigned(std::strlen(count)), count);
            binary_ = true;
        }
    } else return false;
    return true;
}
} // namespace openosc::scpi
