#pragma once
#include "openosc/screenshot/bmp.hpp"
#include "openosc/scpi/backend.hpp"
#include "openosc/scpi/parser.hpp"
#include "openosc/scpi/header.hpp"
#include "openosc/scpi/errors.hpp"

namespace openosc::scope { class Measurements; }
namespace openosc::scpi {
class Engine final : private BlockSink {
public:
    // Bound of a generator operation's wait (above generator::ModuleLink::ExchangeMs, checked in generator.cpp).
    static constexpr uint32_t GeneratorMs = 6000;
    // Bound of a DMM operation's wait (function, AUTO, range: above their own deadlines, checked in meter.cpp).
    static constexpr uint32_t OperationMs = 8000;
    explicit Engine(Backend &backend) : backend_(backend) { parser_.sink(this); }
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;
    bool feed(uint8_t byte, uint32_t now);
    bool endMessage(uint32_t now);
    void poll(uint32_t now);
    size_t read(uint8_t *output, size_t capacity);
    size_t available() const;
    void reset(); // Bus clear/disconnect drops the session, never the acquisition.
    // USBTMC aborts drop one direction only: the command being received, or the pending output
    // (response, waveform block, a waiting *OPC?/reading/waveform). Errors and settings stay.
    void abortInput();
    void discardOutput();
    uint8_t statusByte() const;
    uint32_t outputGeneration() const { return generation_; } // Moves when pending output is dropped (reset, discard).
    bool expire(uint32_t now); // Output left unread 10 s is dropped (-410); true: output pending.
    void idle(uint32_t now); // Instead of poll() (no host, failed recovery): expire(); 10 s idle frees the lease and jobs.
    void closeFile(); // Foreground cleanup after a transfer, abort, or USB stop.
    void refill() { fillStage(); } // Foreground file refill after output, so the background pass can send it.
    size_t unstaged() const; // Reply bytes owed by a foreground producer, including its final LF.
    bool more() const { return unstaged() != 0; }
private:
    bool common(const Command &command);
    bool scopeCommand(const Command &command);
    bool channelCommand(const Command &command);
    bool waveformCommand(const Command &command);
    struct MeterMatch; // A DMM header as the measurement serves it now (meter.cpp).
    bool meterCommand(const Command &command);
    bool meterHoldCommand(const Command &command, MeterMatch &match); // SYSTem:DMM:HOLD:MODE ... :STATe? (meter.cpp).
    bool generatorCommand(const Command &command);
    bool arbitraryCommand(const Command &command); // SOURce:ARBitrary: and SOUR:FUNC ARB's frequency (arbitrary.cpp).
    // BlockSink: SOUR:ARB:DATA's block goes to the backend's stage as it comes (arbitrary.cpp).
    int16_t blockBegin(const char *header, uint32_t bytes) override;
    void blockByte(uint8_t byte) override { backend_.arbitraryByte(this, byte); }
    void blockAbort() override { backend_.arbitraryAbort(this); }
    void dropBlock() { backend_.arbitraryAbort(this); } // A taken block's stage, with the input that held it.
    // SOUR:ARB:DATA?: the loaded waveform as a definite block (Backend::arbitraryRead), pinned to its revision.
    bool arbitraryBinary_ = false;
    uint32_t arbitraryRevision_ = 0, arbitraryAt_ = 0, arbitraryBytes_ = 0;
    bool decodeCommand(const Command &command); // DECode: (decode.cpp).
    bool systemCommand(const Command &command); // Idle settings and MMEMory: (system.cpp), either profile.
    void endFile();
    // MMEM:DATA?: FatFs read in the foreground only (USB's background pass runs inside FAT writes).
    void fillStage();
    static constexpr size_t StageBytes = 512; // A FAT sector a refill.
    static uint8_t *claimStage(const Engine *owner); // One of two ITCM stages (system.cpp); null: both in use.
    static void releaseStage(const Engine *owner);
    bool fileDone() const { return !fileBinary_ || (fileRead_ == fileBytes_ && stageAt_ == stageEnd_); }
    bool fileBinary_ = false, fileClosePending_ = false;
    uint32_t fileRead_ = 0, fileBytes_ = 0;
    uint8_t *stage_ = nullptr; uint16_t stageAt_ = 0, stageEnd_ = 0;
    void endDecode();                           // The CSV export lets go (reset, abort, done).
    void dispatch(const Command &command);
    void reply(const char *format, ...);
    void number(double value);
    void error(int16_t code, bool query = false);
    bool argument(const Command &command, double &value, Header::Unit unit = Header::Unit::Scalar);
    bool boolean(const Command &command, bool &value);
    bool apply(const ScopeRequest &request, bool level = false); // level: its trigger level set (Backend::setTrigger).
    const scope::FrameView *frame(bool refresh = false);
    void meterReading(bool secondary);
    void waitOperation();
    // The generator's own operation (the ZT706S module answers ~0.6 s after a command): commands wait,
    // a missing acknowledge or a stuck UART is -240; the ZT703S is never Busy. Bound: GeneratorMs.
    void waitGenerator();
    Backend &backend_;
    Parser parser_;
    ErrorQueue errors_;
    uint32_t generation_ = 0;
    char response_[256]{};
    size_t responseSize_ = 0, responseAt_ = 0;
    // WAV:DATA?/FRAMe?/ENVelope?: a channel's every stride-th point from first, or (peak) a min/max per bucket.
    struct Segment { uint32_t first = 0, stride = 1, count = 0, end = 0; unsigned channel = 0; };
    Segment segments_[2];
    unsigned segmentCount_ = 0, segmentAt_ = 0;
    uint32_t pointAt_ = 0, binaryBytes_ = 0, binarySent_ = 0;
    bool peakStream_ = false;
    // Peak blocks: fillWave() reduces bounded slices of the pinned record in the foreground into a
    // claimed stage (whole min/max pairs); read() only copies staged bytes, available() counts them.
    static constexpr uint32_t WaveSliceSamples = 32768; // Maximum points scanned in a foreground pass.
    uint8_t *waveStage_ = nullptr; uint16_t waveAt_ = 0, waveEnd_ = 0;
    void fillWave();
    void endWave();
    bool waveMore() const { return binary_ && peakStream_ && binarySent_ + uint32_t(waveEnd_ - waveAt_) < binaryBytes_; }
    // The record a block, its wait or a WAV:MEAS? job is pinned to: another one (or none) pads the
    // block or ends the job with -230.
    uint32_t pinnedSequence_ = 0; uint64_t pinnedTimestamp_ = 0;
    void pin(const scope::FrameMetadata &m) { pinnedSequence_ = m.sequence; pinnedTimestamp_ = m.timestampUs; }
    bool pinned(const scope::FrameView *f) const { return f && f->valid() && f->metadata().sequence == pinnedSequence_ && f->metadata().timestampUs == pinnedTimestamp_; }
    void lostBlock(); // The pinned record went mid-block: zeros complete it, -230, the lease goes.
    bool prepareBlock(const scope::FrameMetadata &m); // Before the header: pin the record, claim a peak stage.
    void openBlock();                                 // After it: the block follows, its first pairs staged.
    // envelope: WAV:ENV?'s buckets over the whole record (type 1), not START/STOP/POIN.
    bool preamble(const scope::FrameMetadata &m, unsigned channel, bool exact, char *out, size_t size, uint32_t envelope = 0) const;
    bool measured(const scope::Measurements &values, bool clipped, unsigned channel, char *out, size_t size); // WAV:MEAS?'s reply.
    void counterFallback(scope::Measurements &values, const scope::FrameMetadata &metadata, uint32_t points) const; // GitHub #171.
    // WAV:MEAS?: one shared job (SCReen or RECord, any session) on the leased record, in bounded
    // slices a poll; endMeasure lets it go.
    static constexpr uint32_t MeasureMs = 10000; // A job's bound (then -230); another session waits to its bound + 1 s.
    bool measureRecord_ = false, measuring_ = false;
    uint32_t ownerUntil_ = 0; // A wait for another session's job ends here (0: none this query).
    void stepMeasure();
    void endMeasure();
    static constexpr uint32_t EnvelopeBuckets = 1500; // WAV:ENV? without a count.
    uint32_t waveEnvelope_ = EnvelopeBuckets; // WAV:ENV? buckets of the query being served.
    bool wavePeak_ = false; // WAV:POIN:MODE PEAK (only with WAV:POIN n).
    uint32_t padding_ = 0; // Zeros still owed to an announced block whose source went (then -230).
    uint32_t waveStart_ = 1, waveStop_ = 0, wavePoints_ = 0; // wavePoints_: WAV:POIN (0: every point).
    unsigned waveChannel_ = 0, preambleChannels_ = 0;
    bool binary_ = false, suffix_ = false, profile_ = false, profileKnown_ = false;
    enum class Completion { None, Wait, Event, Query };
    Completion completion_ = Completion::None;
    bool waitingOperation_ = false, waitingReading_ = false, secondary_ = false;
    bool waitingWaveform_ = false, waitingGenerator_ = false;
    const char *pendingWave_ = "DATA"; // The WAV query waiting for its frame: PRE, DATA, FRAM, ENV, MEAS, SEQ, TIM, KEY, EVEN, STAR or STOP.
    void waitFrame(const char *query, bool fresh = false); // On expiry send NAN and queue -230.
    uint32_t generatorDeadline_ = 0;
    uint32_t readingSequence_ = 0, deadline_ = 0, now_ = 0, inputAt_ = 0, frameAt_ = 0, outputAt_ = 0;
    bool inputPending_ = false;
    // DECode:DATA?: only the export's ticket and the block's progress (the stream itself is the
    // decoder's, in its workspace): measured first (waitingDecode_), then sent as a definite block.
    uint32_t decodeTicket_ = 0, decodeTotal_ = 0, decodeSent_ = 0;
    bool decodeBinary_ = false, waitingDecode_ = false;
    bool screenBinary_ = false; // DISP:DATA?: the BMP from Backend::screenRead, screenAt_ of ScreenBytes sent.
    uint32_t screenTicket_ = 0; // This engine's DISP:DATA? lease of the preview buffer (0: none; Backend::screenEnd).
    void endScreen();
    uint32_t screenAt_ = 0;
    static constexpr uint32_t ScreenBytes = BmpImage::FileBytes;
    uint8_t ese_ = 0, sre_ = 0;
    uint32_t generatorFaults_ = 0; // generator::Snapshot::faults as last seen (Engine::poll).
    bool generatorFaultsKnown_ = false;
};
} // namespace openosc::scpi
