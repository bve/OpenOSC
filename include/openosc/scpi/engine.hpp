#pragma once
#include "openosc/scpi/backend.hpp"
#include "openosc/scpi/parser.hpp"
#include "openosc/scpi/header.hpp"
#include "openosc/scpi/errors.hpp"

namespace openosc::scpi {
class Engine {
public:
    // Bound of a generator operation's wait (above generator::ModuleLink::ExchangeMs, checked in generator.cpp).
    static constexpr uint32_t GeneratorMs = 6000;
    // Bound of a DMM operation's wait (function, AUTO, range: above their own deadlines, checked in meter.cpp).
    static constexpr uint32_t OperationMs = 8000;
    explicit Engine(Backend &backend) : backend_(backend) {}
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
private:
    bool common(const Command &command);
    bool scopeCommand(const Command &command);
    bool channelCommand(const Command &command);
    bool waveformCommand(const Command &command);
    bool meterCommand(const Command &command);
    bool meterHoldCommand(const Command &command); // SYSTem:DMM:HOLD:MODE ... :STATe? (meter.cpp).
    bool generatorCommand(const Command &command);
    bool decodeCommand(const Command &command); // DECode: (decode.cpp).
    void endDecode();                           // The CSV export lets go (reset, abort, done).
    void dispatch(const Command &command);
    void reply(const char *format, ...);
    void number(double value);
    void error(int16_t code, bool query = false);
    bool argument(const Command &command, double &value, Header::Unit unit = Header::Unit::Scalar);
    bool boolean(const Command &command, bool &value);
    bool apply(const ScopeRequest &request);
    const scope::FrameView *frame(bool refresh = false);
    void meterReading(bool secondary);
    void waitOperation();
    // The generator's own operation (the ZT706S module answers ~0.6 s after a command): commands wait,
    // a missing acknowledge or a stuck UART is -240; the ZT703S is never Busy. Bound: GeneratorMs.
    void waitGenerator();
    Backend &backend_;
    Parser parser_;
    ErrorQueue errors_;
    char response_[256]{};
    size_t responseSize_ = 0, responseAt_ = 0;
    uint32_t binaryStart_ = 0, binaryCount_ = 0, binaryAt_ = 0;
    uint32_t waveStart_ = 1, waveStop_ = 0;
    unsigned waveChannel_ = 0, preambleChannels_ = 0;
    bool binary_ = false, suffix_ = false, profile_ = false, profileKnown_ = false;
    enum class Completion { None, Wait, Event, Query };
    Completion completion_ = Completion::None;
    bool waitingOperation_ = false, waitingReading_ = false, secondary_ = false;
    bool waitingWaveform_ = false, pendingPreamble_ = false, waitingGenerator_ = false;
    uint32_t generatorDeadline_ = 0;
    uint32_t readingSequence_ = 0, deadline_ = 0, now_ = 0, inputAt_ = 0, frameAt_ = 0, outputAt_ = 0;
    bool inputPending_ = false;
    // DECode:DATA?: only the export's ticket and the block's progress (the stream itself is the
    // decoder's, in its workspace): measured first (waitingDecode_), then sent as a definite block.
    uint32_t decodeTicket_ = 0, decodeTotal_ = 0, decodeSent_ = 0;
    bool decodeBinary_ = false, waitingDecode_ = false;
    uint8_t ese_ = 0, sre_ = 0;
    uint32_t generatorFaults_ = 0; // generator::Snapshot::faults as last seen (Engine::poll).
    bool generatorFaultsKnown_ = false;
};
} // namespace openosc::scpi
