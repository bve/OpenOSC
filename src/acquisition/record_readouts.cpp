#include "openosc/acquisition/readouts.hpp"
#include <algorithm>
#include <cmath>

namespace openosc::scope {
void RecordReadouts::start(const FrameView &frame, const ReadoutSelection &selection) {
    const auto &m = frame.metadata();
    result_ = {};
    result_.basis = ReadoutBasis::Record;
    result_.timestampUs = m.timestampUs;
    result_.captureRate(m.samplesPerSecond);
    result_.sequence = sequence_ = m.sequence;
    result_.points = samples_ = m.samples;
    metadata_ = m; split_ = frame.split();
    for (unsigned i = 0; i < 4; ++i) banks_[i] = frame.bank(i);
    position_ = 0; pass_ = Pass::Survey; done_ = 0; total_ = 0;
    bool shown = false, edges = false;
    for (unsigned c = 0; c < 2; ++c) {
        auto &lane = lanes_[c];
        lane = {};
        lane.shown = m.channels[c].enabled && selection.needs(c);
        lane.needsEdges = lane.shown && selection.needsEdges(c);
        lane.needsBand = lane.shown && selection.needsBand(c);
        shown |= lane.shown; edges |= lane.needsEdges;
    }
    // Survey, and where any channel may need them the edge pass, a confirming pass and each such
    // channel's tone pass, reserved now and released as each turns out unneeded (or ends early):
    // the percent only rises.
    unsigned toned = 0;
    for (const auto &lane : lanes_) toned += lane.needsEdges;
    if (shown) total_ = samples_ + (edges ? 2 * samples_ + toned * ToneEstimate::cost(samples_) : 0);
    running_ = frame.valid();
}
namespace {
bool sameChannel(const ChannelMetadata &a, const ChannelMetadata &b) {
    return a.enabled == b.enabled && a.calibrated == b.calibrated && a.voltsPerCode == b.voltsPerCode && a.zeroCode == b.zeroCode &&
           a.probeNumerator == b.probeNumerator && a.probeDenominator == b.probeDenominator &&
           a.millivoltsPerDivision == b.millivoltsPerDivision && a.analogPosition == b.analogPosition &&
           a.coupling == b.coupling && a.calibrationCrc == b.calibrationCrc;
}
bool sameSplit(const SplitHistory &a, const SplitHistory &b) {
    if (a.ahbWords != b.ahbWords || a.historyWords != b.historyWords) return false;
    for (unsigned i = 0; i < 4; ++i)
        if (a.older[i] != b.older[i] || a.end[i] != b.end[i] || a.newest[i] != b.newest[i]) return false;
    return true;
}
} // namespace
bool RecordReadouts::sameRecord(const FrameView &frame) const {
    const auto &m = frame.metadata(), &s = metadata_;
    if (!frame.valid() || m.sequence != s.sequence || m.timestampUs != s.timestampUs || m.samples != s.samples ||
        m.ring != s.ring || m.origin != s.origin || m.layout != s.layout || m.rolling != s.rolling ||
        m.samplesPerSecond.numerator != s.samplesPerSecond.numerator || m.samplesPerSecond.denominator != s.samplesPerSecond.denominator ||
        !sameChannel(m.channels[0], s.channels[0]) || !sameChannel(m.channels[1], s.channels[1]) || !sameSplit(frame.split(), split_))
        return false;
    for (unsigned i = 0; i < 4; ++i)
        if (frame.bank(i) != banks_[i]) return false;
    return true;
}
uint8_t RecordReadouts::percent() const {
    return total_ ? uint8_t(std::min<uint64_t>(99, uint64_t(done_) * 100 / total_)) : 0;
}
// A column's extremes, or the part of it a slice reached: a cut column is completed by the next.
void RecordReadouts::Lane::column(const ColumnStats &stats, bool complete) {
    int16_t columnLow = stats.low, columnHigh = stats.high;
    if (cut) { columnLow = std::min(columnLow, low); columnHigh = std::max(columnHigh, high); }
    cut = !complete;
    if (complete) band.add(columnLow, columnHigh);
    else { low = columnLow; high = columnHigh; }
}
// The band columns of [position, position + n) for each channel that needs its band (edge
// thresholds, robust levels), SignalBands' columns: aligned to the record's start, the last one
// ending with it. Nothing is read when no channel needs one (moments-only readouts).
bool RecordReadouts::bands(const FrameView &frame, uint32_t n) {
    if (!lanes_[0].needsBand && !lanes_[1].needsBand) return true;
    for (uint32_t end = position_ + n, at = position_; at < end;) {
        const uint32_t k = std::min(end - at, SignalBands::Column - at % SignalBands::Column);
        const bool complete = (at + k) % SignalBands::Column == 0 || at + k == samples_;
        if (frame.pairs()) { // Both channels from one read of each word.
            ColumnStats stats[2];
            if (frame.pairCursor(at).column(k, stats) != k) return false;
            for (unsigned c = 0; c < 2; ++c)
                if (lanes_[c].needsBand) {
                    lanes_[c].column(stats[c], complete);
                    lanes_[c].levels.addColumn(frame, Channel(c), at / SignalBands::Column, at, at + k);
                }
        } else {
            for (unsigned c = 0; c < 2; ++c) {
                if (!lanes_[c].needsBand) continue;
                ColumnStats stats;
                if (frame.cursor(Channel(c), at).column(k, stats) != k) return false;
                lanes_[c].column(stats, complete);
                lanes_[c].levels.addColumn(frame, Channel(c), at / SignalBands::Column, at, at + k);
            }
        }
        at += k;
    }
    return true;
}
bool RecordReadouts::survey(const FrameView &frame, uint32_t n) {
    for (unsigned c = 0; c < 2; ++c) {
        if (!lanes_[c].shown) continue;
        dsp::Moments part;
        if (!RecordMoments::measure(frame, Channel(c), position_, n, part)) return false;
        lanes_[c].moments.merge(part);
    }
    return bands(frame, n);
}
// The edge pass (or the confirming one) of [position, position + n), in the scanners' columns.
bool RecordReadouts::walk(const FrameView &frame, uint32_t n) {
    EdgeScanner *scanners[2] = {};
    for (unsigned c = 0; c < 2; ++c) {
        auto &lane = lanes_[c];
        if (pass_ == Pass::Edges ? lane.edges.active() : lane.confirming) scanners[c] = &lane.edges;
    }
    for (uint32_t end = position_ + n, at = position_; at < end;) {
        const uint32_t k = std::min(end - at, EdgeScanner::Column - at % EdgeScanner::Column);
        EdgeScanner::walk(frame, at, k, scanners);
        at += k;
    }
    return true;
}
// The moments are complete: extrema, mean and RMS; the edge thresholds from the band.
void RecordReadouts::surveyed() {
    bool edges = false;
    for (auto &lane : lanes_) {
        if (!lane.shown) continue;
        auto &s = lane.summary;
        s.valid = lane.moments.count == samples_;
        s.minimum = lane.moments.minimum; s.maximum = lane.moments.maximum;
        s.mean = double(lane.moments.sum) / samples_;
        s.rms = std::sqrt(double(lane.moments.squares) / samples_);
        s.hasPulses = lane.needsEdges; // No edge pass, no pulse count.
        const auto band = lane.needsBand ? lane.band.band() : SignalBand{};
        s.hasRobust = band.valid;
        if (band.valid) { s.robustMinimum = band.low; s.robustMaximum = band.high; }
        if (lane.needsEdges && s.maximum - s.minimum >= 4) {
            const SignalBand thresholds = lane.levels.thresholds(band);
            lane.edges.begin(thresholds.low, thresholds.high, EdgeScanner::laneBurst(metadata_));
        }
        edges |= lane.edges.active();
    }
    for (const auto &lane : lanes_)
        if (lane.needsEdges && !lane.edges.active()) total_ -= ToneEstimate::cost(samples_); // A flat band: no tone either.
    if (edges) pass_ = Pass::Edges;
    else if (lanes_[0].needsEdges || lanes_[1].needsEdges) total_ -= 2 * samples_; // The edge and confirming passes a flat band skips.
}
bool RecordReadouts::edged() {
    const double rate = metadata_.samplesPerSecond.value();
    bool toning = false;
    for (auto &lane : lanes_) {
        if (!lane.shown) continue;
        lane.edges.close(); // The last pass's last burst.
        lane.edges.finish(rate, lane.summary);
        lane.toning = ToneEstimate::wanted(lane.summary, rate, lane.edges.active());
        toning |= lane.toning;
        if (lane.edges.active() && !lane.toning) total_ -= ToneEstimate::cost(samples_);
    }
    toneSpent_ = 0;
    for (auto &lane : lanes_) if (lane.toning) { tone_.begin(samples_, lane.summary.mean); break; }
    return toning;
}
bool RecordReadouts::tones(const FrameView &frame, uint32_t &budget) {
    for (unsigned c = 0; c < 2; ++c) {
        auto &lane = lanes_[c];
        if (!lane.toning) continue;
        const uint32_t before = budget;
        const bool done = tone_.step(frame, Channel(c), budget);
        done_ += before - budget; toneSpent_ += before - budget;
        if (!done) return false;
        ToneEstimate::apply(lane.summary, tone_.result(), metadata_.samplesPerSecond.value());
        total_ -= ToneEstimate::cost(samples_) - toneSpent_; // A tone found early (or none: the coarse stage only).
        toneSpent_ = 0;
        lane.toning = false;
        for (unsigned next = c + 1; next < 2; ++next)
            if (lanes_[next].toning) { tone_.begin(samples_, lanes_[next].summary.mean); break; }
    }
    return true;
}
void RecordReadouts::finish(const FrameView &frame, uint32_t now) {
    for (unsigned c = 0; c < 2; ++c) {
        auto &lane = lanes_[c];
        if (!lane.shown) continue;
        result_.values[c] = Measurements::calculate(lane.summary, frame.metadata().channels[c]);
        result_.computed[c] = true;
    }
    running_ = false;
    result_.valid = true; result_.finishedAt = now;
}
bool RecordReadouts::step(const FrameView &frame, uint32_t budget, uint32_t now) {
    if (!running_) return result_.valid;
    if (!sameRecord(frame)) { running_ = false; return false; } // Not the record started on: no result.
    if (!total_) { finish(frame, now); return true; }            // Nothing selected on a channel that is on.
    while (budget) {
        if (pass_ == Pass::Tone) {
            if (!tones(frame, budget)) return false;
            finish(frame, now);
            return true;
        }
        const uint32_t n = std::min(budget, samples_ - position_);
        if (!(pass_ == Pass::Survey ? survey(frame, n) : walk(frame, n))) { running_ = false; return false; }
        position_ += n; budget -= n; done_ += n;
        if (position_ < samples_) continue;
        position_ = 0;
        if (pass_ == Pass::Survey) {
            surveyed();
            if (pass_ == Pass::Edges) continue;
        } else if (pass_ == Pass::Edges) {
            bool confirming = false;
            for (auto &lane : lanes_) {
                if (!lane.edges.active()) continue;
                lane.edges.close();
                if (const double candidate = lane.edges.retryCandidate()) {
                    lane.edges.confirm(candidate); lane.confirming = confirming = true;
                }
            }
            if (confirming) { pass_ = Pass::Confirm; continue; }
            total_ -= samples_; // No confirming pass.
        }
        if (edged()) { pass_ = Pass::Tone; continue; }
        finish(frame, now);
        return true;
    }
    return false;
}
} // namespace openosc::scope
