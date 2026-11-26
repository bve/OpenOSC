#pragma once
#include "openosc/acquisition/background_reader.hpp"
#include "openosc/acquisition/mailbox.hpp"
#include "openosc/decode/machine.hpp"

namespace openosc::decoding {
// The decoding of one record at a time (plan §5.2, §5.3), in slices of the main loop.
// RUN Complete and HOLD: offer() hands it the lease of the record on show; it keeps that lease (in a
// one-slot geometry the next capture then waits: the backpressure) until the whole record is read,
// then publishes the result and releases it. A newer record offered meanwhile does not stop it: it
// is counted as skipped. DataAccess may revoke the lease (configuration, HOLD/RUN, zero, shutdown):
// the record is then abandoned and its partial result never published. One record whose key and
// settings match the published result is not decoded again.
class DecodeJob final : public scope::BackgroundReader {
public:
    struct Budget {
        uint32_t (*cycles)() = nullptr; // A free-running cycle counter (DWT on the device).
        uint32_t limit = 0;             // Cycles a slice may take; checked after each block.
        uint32_t nowMs = 0;             // The time a record read through is shared with hosts at.
    };
    // New settings: a running record is abandoned and the published result kept only if it was
    // decoded with the same parameters. Disabled: nothing held, nothing published.
    void configure(const Settings &settings);
    const Settings &settings() const { return settings_; }
    // A lease of the record to decode (moved in only when taken). False: kept by the caller.
    bool offer(scope::FrameLease &lease);
    // Whether offer() would take a record with this metadata now (idle, a different key or settings).
    bool wants(const scope::FrameMetadata &metadata) const;
    // One slice: blocks until the record ends or the budget is used. True while a record runs.
    bool step(const Budget &budget);
    void revoke() override;
    // Hand-over (RUN Complete under a drawn trace): a record read through is kept, not released,
    // until the display takes it (handOver), so the trace shows exactly the record its result is
    // of. Off (Decoder only, nothing to show it): released at the end, as before. Turning it off
    // releases a record still waiting.
    void keepForDisplay(bool keep);
    bool handOver(scope::FrameLease &out); // The decoded record waiting for the display, if any.
    // The host share (plan §5.3: UI and USB get the same record in Complete, all three views). While
    // hosts ask for records (hostDemand), the record read through is shared before it is released or
    // handed to the display: it then stays, past the UI's hand-over and in Decoder only too, for the
    // USB readers (takeHostRecord: a lease of their own), until the controller drops it (every host
    // took it, DemandMs, HoldMs: FrameHosts::owed; RUN/SINGle re-arm) or anything revokes the job
    // (configuration, HOLD on/off, zero, shutdown, a new source, Off, other decode parameters).
    void hostDemand(bool demanding) { hostDemand_ = demanding; }
    bool takeHostRecord(scope::FrameLease &out) const { return hostShare_.share(out); }
    const scope::FrameView *hostRecord() const { return hostShare_.valid() ? &hostShare_.view() : nullptr; }
    uint32_t hostSharedAt() const { return hostSharedAt_; }
    void dropHostRecord() { hostShare_.release(); }
    // A new source of records (a RAW file loaded, a capture session restarted by its owner): no
    // record of before is the same as one after, even with equal sequence, timestamp and metadata.
    // The running record is let go and the published result dropped.
    void invalidate();
    // The published result if it is of exactly this record (full RecordKey, this generation, and
    // the settings in force), else none: annotations are never drawn over another record.
    const ResultSet *shows(const scope::FrameMetadata &metadata) const;
    // UART autobaud (plan §6.3; RS-232 and RS-485 too, CH1 - CH2 included): a hint, never applied here. On a held record (HOLD's, offered by the
    // controller): the record's transitions through BaudEstimator (a candidate), then all of them again
    // through BaudValidator (that rate, half and a third: exactly one clean, at least MinFrames correct
    // characters and at most one bad per eight). Bounded slices as a decode; revoked as one; decoding
    // waits meanwhile (no record is taken). False: not UART, a record running, no scale, channel off.
    struct BaudSearch {
        enum class State : uint8_t { Idle, Running, Done, Cancelled };
        State state = State::Idle;
        protocols::BaudHint hint;  // Done: hint.confirmed only may be offered to the user.
        RecordKey key;             // The record it was found on.
        uint32_t settingsHash = 0; // The settings in force then (a change makes it stale).
    };
    bool searchBaud(scope::FrameLease &lease);
    const BaudSearch &baudSearch() const { return search_; }
    bool measuring() const { return measuring_ && lease_.valid(); } // Auto: the record's swing is being read.
    unsigned searchPercent() const { return task_ != Task::Decode && lease_.valid() && lease_.view().metadata().samples
        ? unsigned(uint64_t(position_) * 100 / lease_.view().metadata().samples) : 0; }
    bool running() const { return lease_.valid(); }
    const ResultSet *decoding() const { return lease_.valid() && task_ == Task::Decode ? &results_.building() : nullptr; } // Its progress.
    const ResultSets &results() const { return results_; }
    ResultSets &results() { return results_; } // An export's lease (CsvExport).
    uint32_t skipped() const { return skipped_; } // Records offered while one ran, each once (the status's skip).
    uint32_t generation() const { return generation_; }
private:
    enum class Task : uint8_t { Decode, Estimate, Validate };
    bool stepSearch(const Budget &budget);
    void finishSearch(BaudSearch::State state);
    void finishRecord(DecodeStatus status, uint32_t nowMs);
    RecordKey keyOf(const scope::FrameMetadata &metadata) const;
    // The record's source channels for the settings: enabled and recorded, sampled as pairs for CH1 - CH2,
    // DC coupled where the protocol needs it; Decoding when usable, else the status to say.
    DecodeStatus sourceStatus(const scope::FrameMetadata &metadata) const;
    DecodeStatus prepare(const scope::FrameMetadata &metadata, ResultSet &set); // Sources, scale, machine; Decoding when ready.
    void beginSearchStream(); // A baud search's stream: searchCodes_, or CH1 - CH2 with searchHysteresisMv_.
    DecodeStatus beginStreams(const scope::FrameMetadata &metadata, ResultSet &set); // Each line's hysteresis, its stream.
    bool measure(const scope::FrameView &frame, const Budget &budget, uint32_t start); // Auto: the swing; true once read.
    unsigned scanBlock(const scope::FrameView &frame, uint32_t n); // Both lines' transitions, merged by index.
    void giveLevels(); // Two-line protocols: each line's first clear level, once known, before its transitions.
    void beginLine(ResultSet &set);                   // One line: started at its first known level.
    void finishLine(ResultSet &set, uint32_t samples); // One line: finished, its events back to record indices.
    Settings settings_;
    scope::FrameLease lease_, decoded_; // The record being read; the one read through, for the display.
    scope::FrameLease hostShare_;       // The record read through, for the hosts (hostDemand).
    uint32_t hostSharedAt_ = 0;
    bool hostDemand_ = false;
    RecordKey running_; // The record being decoded.
    RecordKey skippedKey_; // The last record skipped while it ran: counted once, not on every pass.
    ResultSets results_;
    // Plan §5.2: one line in blocks of 128 points, two lines in blocks of 64; the second line's
    // transitions in second_, then merged with the first's (scanned to the top of transitions_). Each
    // block goes to the machine at once, so plan §5.4's 2 x 512 transition rings are not needed (Root):
    // a point gives at most one transition of its line, so a block holds at most 128 (one line) or
    // 2 x 64 (two lines) and these arrays are never short (worstCaseBlocks, the native suite).
    static constexpr uint32_t PairBlock = ThresholdStream::Block / 2;
    ProtocolMachine machine_;
    ThresholdStream streams_[2];
    LevelRange ranges_[2];      // Auto hysteresis: each line's swing in the record (measuring_).
    uint8_t lineChannel_[2] = {0, 1};
    uint32_t minPulse_ = 0;     // In points of the running record.
    bool measuring_ = false;
    DifferenceScale difference_;
    Transition transitions_[2 * PairBlock + 2], second_[PairBlock + 1];
    uint32_t position_ = 0, generation_ = 1, lastSequence_ = 0, skipped_ = 0, block_ = ThresholdStream::Block;
    uint8_t lines_ = 1;                 // Streams of the protocol: 1, or 2 (I2C, SPI).
    bool levelGiven_[2] = {false, false};
    bool started_ = false;   // The machine was started for this record.
    uint32_t origin_ = 0;    // One line: the record index the machine counts from (its first known level).
    bool seen_ = false, keep_ = false;
    Task task_ = Task::Decode;
    BaudSearch search_;
    ThresholdCodes searchCodes_;
    uint32_t searchHysteresisMv_ = 0;
    bool searchDifference_ = false;
    protocols::BaudEstimator *estimator_ = nullptr;
    protocols::BaudValidator *validator_ = nullptr;
};
} // namespace openosc::decoding
