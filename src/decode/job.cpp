#include "openosc/decode/job.hpp"
#include <cmath>

namespace openosc::decoding {
void DecodeJob::configure(const Settings &settings) {
    if (settings == settings_) return;
    const bool same = settings.enabled && settings_.enabled && settings.hash() == settings_.hash();
    const bool start = settings.enabled && !settings_.enabled;
    settings_ = settings;
    if (start) { invalidate(); return; } // A new start: no result of before.
    if (same) return;                    // The view only: the result stands, a running record goes on.
    revoke();
    if (!settings.enabled) results_.revoke(); // Off: an export lets go (its set may be rewritten next).
    results_.clear(); // Other parameters: a held set stays whole (its own settings hash) until released.
}
void DecodeJob::invalidate() {
    ++generation_;
    revoke();
    results_.revoke(); // A new source: no export of a record of before goes on.
    results_.clear();
}
void DecodeJob::keepForDisplay(bool keep) {
    keep_ = keep;
    if (!keep) decoded_.release();
}
bool DecodeJob::handOver(scope::FrameLease &out) {
    if (!decoded_.valid()) return false;
    out = std::move(decoded_);
    return true;
}
const ResultSet *DecodeJob::shows(const scope::FrameMetadata &metadata) const {
    const auto *shown = results_.published();
    return settings_.enabled && shown && shown->key() == keyOf(metadata) && shown->settingsHash() == settings_.hash() ? shown : nullptr;
}
RecordKey DecodeJob::keyOf(const scope::FrameMetadata &metadata) const { return RecordKey::of(metadata, generation_); }
bool DecodeJob::wants(const scope::FrameMetadata &metadata) const {
    if (!settings_.enabled || task_ != Task::Decode) return false; // A baud search runs: no record taken.
    if (lease_.valid()) { const auto key = keyOf(metadata); return key != running_ && key != skippedKey_; } // Skipped, once.
    if (!results_.buildingFree()) return false; // The set to write is held by an export: wait for it.
    const auto *shown = results_.published();
    return !shown || shown->key() != keyOf(metadata) || shown->settingsHash() != settings_.hash();
}
// The record's sources against the settings (plan §5.3): each line's channel in the record, its
// threshold in codes (or the difference's scale), the protocol's timing at this Fs. Anything else is a
// status published at once, never a guess. Manual hysteresis: the streams begin now. Auto: each line's
// swing is measured first (measuring_, bounded slices of step()), then beginStreams().
DecodeStatus DecodeJob::prepare(const scope::FrameMetadata &m, ResultSet &set) {
    const unsigned sources = settings_.sources();
    if (!sources) return DecodeStatus::Unsupported;
    for (unsigned c = 0; c < 2; ++c)
        if ((sources >> c & 1) && (!m.channels[c].enabled || (m.layout == scope::Layout::InterleavedCh1 && c)))
            return DecodeStatus::ChannelOff;
    minPulse_ = 0; // The minimum pulse in points of this record (0: off), rounded up.
    if (settings_.minPulseNs && m.samplesPerSecond.valid())
        minPulse_ = uint32_t(std::ceil(double(settings_.minPulseNs) * m.samplesPerSecond.value() / 1e9));
    const bool difference = settings_.protocol == Protocol::Can && settings_.can.source == CanSource::Difference;
    const bool automatic = settings_.hysteresis == Hysteresis::Auto;
    lines_ = difference ? 1 : uint8_t(sources == 3 ? 2 : 1);
    block_ = lines_ == 2 ? PairBlock : ThresholdStream::Block;
    set.lines = uint8_t(sources); set.difference = difference; set.hysteresisAuto = automatic;
    streams_[0] = streams_[1] = ThresholdStream{};
    if (difference) {
        if (m.layout != scope::Layout::Linear && m.layout != scope::Layout::FourBanks) return DecodeStatus::ChannelOff;
        if (!DifferenceScale::of(m, difference_)) return DecodeStatus::NoScale;
        set.thresholdMv[0] = settings_.can.differenceMv;
        ranges_[0].beginDifference(difference_);
    } else {
        unsigned line = 0;
        for (unsigned c = 0; c < 2; ++c) {
            if (!(sources >> c & 1)) continue;
            // The scale and the threshold itself now (Auto: its band is known once the swing is).
            const int32_t threshold = settings_.threshold(c);
            const auto codes = ThresholdCodes::of(m.channels[c], threshold, automatic ? 0 : settings_.hysteresisMv);
            if (codes.status != ThresholdCodes::Status::Ok)
                return codes.status == ThresholdCodes::Status::NoScale ? DecodeStatus::NoScale : DecodeStatus::OutOfRange;
            set.thresholdMv[c] = threshold;
            lineChannel_[line] = uint8_t(c);
            ranges_[line++].begin(scope::Channel(c));
        }
    }
    if (!machine_.configure(settings_, m)) return DecodeStatus::Timing;
    levelGiven_[0] = levelGiven_[1] = false;
    measuring_ = set.measuring = automatic;
    return automatic ? DecodeStatus::Decoding : beginStreams(m, set);
}
// Each line's hysteresis (Auto: from the swing just measured; Manual: the setting) into the set, then
// its stream from the record's first point.
DecodeStatus DecodeJob::beginStreams(const scope::FrameMetadata &m, ResultSet &set) {
    const bool automatic = settings_.hysteresis == Hysteresis::Auto;
    const unsigned percent = Protocols::AutoHysteresisPercent;
    measuring_ = set.measuring = false;
    position_ = 0; set.processed = 0;
    if (set.difference) {
        const uint32_t h = automatic ? difference_.autoHysteresisMv(ranges_[0].swing(), percent) : settings_.hysteresisMv;
        set.hysteresisMv[0] = h;
        streams_[0].beginDifference(difference_, settings_.can.differenceMv, h, minPulse_, 0);
    } else {
        for (unsigned line = 0; line < lines_; ++line) {
            const unsigned c = lineChannel_[line];
            const uint32_t h = automatic ? ThresholdCodes::autoHysteresisMv(m.channels[c], uint32_t(ranges_[line].swing()), percent)
                                         : settings_.hysteresisMv;
            const auto codes = ThresholdCodes::of(m.channels[c], settings_.threshold(c), h);
            set.hysteresisMv[c] = h;
            if (codes.status != ThresholdCodes::Status::Ok)
                return codes.status == ThresholdCodes::Status::NoScale ? DecodeStatus::NoScale : DecodeStatus::OutOfRange;
            streams_[line].begin(codes, minPulse_, scope::Channel(c));
        }
    }
    set.levelsKnown = true;
    return DecodeStatus::Decoding;
}
// Auto hysteresis: the lines' swing, block by block (one line in blocks of 128, as the decode); true
// once the whole record was read.
bool DecodeJob::measure(const scope::FrameView &frame, const Budget &budget, uint32_t start) {
    const uint32_t samples = frame.metadata().samples;
    while (position_ < samples) {
        const uint32_t n = samples - position_ < ThresholdStream::Block ? samples - position_ : ThresholdStream::Block;
        for (unsigned line = 0; line < lines_; ++line) ranges_[line].scan(frame, position_, n);
        position_ += n;
        if (budget.cycles && budget.cycles() - start >= budget.limit) break;
    }
    return position_ >= samples;
}
bool DecodeJob::offer(scope::FrameLease &lease) {
    if (!settings_.enabled || !lease.valid()) return false;
    const auto &m = lease.view().metadata();
    // A sequence before the last record taken: a new capture session (its owner should also call
    // invalidate()). Records only looked at (skipped while one runs) never count here: in two slots
    // the newer one is seen first and HOLD may then keep the older one.
    if (seen_ && int32_t(m.sequence - lastSequence_) < 0) ++generation_;
    if (!wants(m)) return false;
    if (lease_.valid()) { skippedKey_ = keyOf(m); ++skipped_; return false; } // Complete: the running record goes on.
    if (!results_.buildingFree()) return false; // Held by an export (wants() is false too): nothing reset.
    seen_ = true; lastSequence_ = m.sequence;
    decoded_.release(); // Not taken by the display: the newer record replaces it.
    auto &set = results_.building();
    running_ = keyOf(m);
    set.reset(running_, settings_.hash(), settings_.protocol);
    set.samples = m.samples;
    set.channel = uint8_t(settings_.protocol == Protocol::Can ? (settings_.can.source == CanSource::RxdCh2) : settings_.channel);
    set.samplesPerSecond = m.samplesPerSecond; set.triggerIndex = m.triggerIndex; set.triggered = m.triggered;
    set.window = ProtocolMachine::window(m);
    position_ = 0;
    const auto status = prepare(m, set);
    if (status != DecodeStatus::Decoding) { set.status = status; results_.publish(); return false; }
    // Two lines: started now, each line's level given once known (giveLevels). One line: started when the
    // stream first knows its level (beginLine), from that point on.
    started_ = lines_ == 2;
    origin_ = 0;
    if (started_) machine_.decoder().start(set, -1);
    lease_ = std::move(lease);
    return true;
}
// One line (UART, LIN, CAN): the machine starts once, at the first point whose level the threshold knows
// (ThresholdStream::initialIndex), with that level, in coordinates from that point: an unclear prefix (in the
// hysteresis band) is never taken as the level held since the record began (a break from point 0, a long
// idle). Its events are moved back to record indices at the end (finishLine).
void DecodeJob::beginLine(ResultSet &set) {
    if (started_ || streams_[0].initialLevel() < 0) return;
    origin_ = streams_[0].initialIndex();
    machine_.decoder().start(set, streams_[0].initialLevel());
    started_ = true;
}
void DecodeJob::finishLine(ResultSet &set, uint32_t samples) {
    if (!started_) return; // No point of a known level: nothing to decode, nothing invented.
    machine_.decoder().finish(samples - origin_, set);
    if (!origin_) return;
    for (unsigned i = 0; i < set.count(); ++i) { auto &e = set.event(i); e.first += origin_; e.last += origin_; }
}
void DecodeJob::giveLevels() {
    if (lines_ != 2) return;
    for (unsigned k = 0; k < 2; ++k)
        if (!levelGiven_[k] && streams_[k].initialLevel() >= 0) {
            machine_.initialLevel(uint8_t(k), streams_[k].initialLevel()); // Line k is channel k (both read).
            levelGiven_[k] = true;
        }
}
unsigned DecodeJob::scanBlock(const scope::FrameView &frame, uint32_t n) {
    if (lines_ == 1) return streams_[0].scan(frame, position_, n, transitions_);
    // The first line to the top of transitions_, the second to second_; merged from the bottom up: an
    // output slot (i + j) never passes the first line's next unread one (PairBlock + 1 + i).
    Transition *a = transitions_ + PairBlock + 1;
    const unsigned na = streams_[0].scan(frame, position_, n, a), nb = streams_[1].scan(frame, position_, n, second_);
    unsigned i = 0, j = 0, k = 0;
    while (i < na || j < nb) {
        if (j == nb || (i < na && a[i].index <= second_[j].index)) { const Transition t = a[i++]; transitions_[k++] = t; }
        else transitions_[k++] = second_[j++];
    }
    return k;
}
bool DecodeJob::searchBaud(scope::FrameLease &lease) {
    if (!settings_.enabled || !Protocols::uartFraming(settings_.protocol) || lease_.valid() || !lease.valid()) return false;
    const auto &m = lease.view().metadata();
    const unsigned channel = settings_.channel;
    if (!m.channels[channel].enabled || (m.layout == scope::Layout::InterleavedCh1 && channel)) return false;
    const bool automatic = settings_.hysteresis == Hysteresis::Auto; // Auto: the swing first, as a decode.
    searchCodes_ = ThresholdCodes::of(m.channels[channel], settings_.threshold(channel), automatic ? 0 : settings_.hysteresisMv);
    if (searchCodes_.status != ThresholdCodes::Status::Ok || !m.samplesPerSecond.valid()) return false;
    decoded_.release();
    measuring_ = automatic;
    if (automatic) ranges_[0].begin(scope::Channel(channel));
    else streams_[0].begin(searchCodes_, 0, scope::Channel(channel)); // No minimum pulse: the edges as they are.
    estimator_ = &machine_.estimator(); estimator_->reset();
    search_ = {}; search_.state = BaudSearch::State::Running; search_.key = keyOf(m); search_.settingsHash = settings_.hash();
    task_ = Task::Estimate; position_ = 0; lines_ = 1;
    lease_ = std::move(lease);
    return true;
}
void DecodeJob::finishSearch(BaudSearch::State state) {
    search_.state = state;
    task_ = Task::Decode;
    estimator_ = nullptr; validator_ = nullptr; measuring_ = false;
    lease_.release();
}
bool DecodeJob::stepSearch(const Budget &budget) {
    const auto &frame = lease_.view();
    const auto &m = frame.metadata();
    const uint32_t start = budget.cycles ? budget.cycles() : 0;
    if (measuring_) {
        if (!measure(frame, budget, start)) return true;
        const unsigned channel = settings_.channel;
        const uint32_t h = ThresholdCodes::autoHysteresisMv(m.channels[channel], uint32_t(ranges_[0].swing()),
                                                            Protocols::AutoHysteresisPercent);
        searchCodes_ = ThresholdCodes::of(m.channels[channel], settings_.threshold(channel), h);
        if (searchCodes_.status != ThresholdCodes::Status::Ok) { finishSearch(BaudSearch::State::Done); return false; }
        streams_[0].begin(searchCodes_, 0, scope::Channel(channel));
        measuring_ = false; position_ = 0;
        return true;
    }
    while (position_ < m.samples) {
        const uint32_t n = m.samples - position_ < ThresholdStream::Block ? m.samples - position_ : ThresholdStream::Block;
        const unsigned count = streams_[0].scan(frame, position_, n, transitions_);
        if (task_ == Task::Estimate) estimator_->feed(transitions_, count);
        else validator_->feed(transitions_, count);
        position_ += n;
        if (budget.cycles && budget.cycles() - start >= budget.limit) return true;
    }
    if (task_ == Task::Estimate) { // A candidate bit time, then every transition again at it, half and a third.
        const uint64_t q16 = estimator_->pointsQ16();
        protocols::CharacterFormat format;
        format.dataBits = settings_.uart.dataBits; format.parity = protocols::Parity(settings_.uart.parity);
        format.stopHalfBits = settings_.uart.stopHalfBits; format.msbFirst = settings_.uart.msbFirst;
        validator_ = &machine_.validator(); // In place of the estimator: its candidate is kept in q16.
        estimator_ = nullptr;
        if (!q16 || !validator_->begin(q16, format, settings_.uartInverted(), m.samplesPerSecond)) { finishSearch(BaudSearch::State::Done); return false; }
        streams_[0].begin(searchCodes_, 0, scope::Channel(settings_.channel));
        task_ = Task::Validate; position_ = 0;
        return true;
    }
    validator_->finish(m.samples);
    search_.hint = validator_->result();
    finishSearch(BaudSearch::State::Done);
    return false;
}
bool DecodeJob::step(const Budget &budget) {
    if (!lease_.valid()) return false;
    if (task_ != Task::Decode) return stepSearch(budget);
    auto &set = results_.building();
    const auto &frame = lease_.view();
    const uint32_t samples = frame.metadata().samples;
    const uint32_t start = budget.cycles ? budget.cycles() : 0;
    if (measuring_) { // Auto hysteresis: the swing of this very record first (bounded, as the decode).
        const bool measured = measure(frame, budget, start);
        if (budget.cycles) set.cycles += budget.cycles() - start;
        set.processed = position_;
        if (!measured) return true;
        const auto status = beginStreams(frame.metadata(), set);
        if (status != DecodeStatus::Decoding) { finishRecord(status, budget.nowMs); return false; }
        return true; // The decode in the next slice.
    }
    auto &decoder = machine_.decoder();
    while (position_ < samples) {
        const uint32_t n = samples - position_ < block_ ? samples - position_ : block_;
        const unsigned count = scanBlock(frame, n);
        if (lines_ == 2) giveLevels();
        else {
            beginLine(set);
            for (unsigned i = 0; i < count; ++i) transitions_[i].index -= origin_; // After the level is known.
        }
        if (started_) decoder.feed(transitions_, count, set);
        position_ += n;
        if (budget.cycles && budget.cycles() - start >= budget.limit) break;
    }
    if (budget.cycles) set.cycles += budget.cycles() - start;
    set.processed = position_;
    if (position_ < samples) return true;
    if (lines_ == 2) { giveLevels(); decoder.finish(samples, set); }
    else finishLine(set, samples);
    set.initialLevel = int8_t(streams_[0].initialLevel());
    finishRecord(DecodeStatus::Done, budget.nowMs);
    return false;
}
void DecodeJob::finishRecord(DecodeStatus status, uint32_t nowMs) {
    auto &set = results_.building();
    set.status = status;
    set.glitches = set.skipped = set.clipped = 0;
    for (unsigned k = 0; k < lines_; ++k) {
        set.glitches += streams_[k].glitches(); set.skipped += streams_[k].skippedPoints(); set.clipped += streams_[k].clipped();
    }
    set.dropped = machine_.dropped(); set.overflows = machine_.overflows();
    set.flagsSeen = 0;
    for (unsigned i = 0; i < set.count(); ++i) set.flagsSeen |= set.event(i).flags;
    results_.publish();
    if (hostDemand_) { // Before the display takes it or it is let go: the hosts' copy of this very record.
        hostShare_.release();
        if (lease_.share(hostShare_)) hostSharedAt_ = nowMs;
    }
    if (keep_) decoded_ = std::move(lease_);
    else lease_.release();
}
void DecodeJob::revoke() {
    hostShare_.release(); // Whatever revokes the job (HOLD, configuration, a new source, Off) frees the hosts' copy.
    measuring_ = false;
    if (task_ != Task::Decode) { finishSearch(BaudSearch::State::Cancelled); return; } // Never the result sets.
    decoded_.release();
    if (!lease_.valid()) return;
    lease_.release(); // Abandoned: the published result is the one before it.
    results_.building().status = DecodeStatus::Cancelled;
}
} // namespace openosc::decoding
