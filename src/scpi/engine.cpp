#include "openosc/scpi/engine.hpp"
#include "openosc/data/decimal.hpp"
#include "openosc/platform/usb_trace.hpp"
#include <cstdarg>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace openosc::scpi {
bool Engine::feed(uint8_t byte, uint32_t now) {
    if (!parser_.feed(byte)) return false;
    inputAt_ = now; inputPending_ = byte != '\n'; return true;
}
bool Engine::endMessage(uint32_t now) {
    if (!parser_.endMessage()) return false;
    inputAt_ = now; inputPending_ = false; return true;
}
void Engine::abortInput() { parser_.reset(); inputPending_ = false; }
void Engine::endDecode() {
    if (decodeTicket_) backend_.decodeEnd(decodeTicket_);
    decodeTicket_ = decodeTotal_ = decodeSent_ = 0;
    decodeBinary_ = waitingDecode_ = false;
}
void Engine::endFile() { if (fileBinary_) backend_.fileClose(); fileBinary_ = false; fileAt_ = fileBytes_ = 0; }
void Engine::endScreen() {
    if (screenTicket_) backend_.screenEnd(screenTicket_);
    screenTicket_ = 0;
}
void Engine::discardOutput() {
    ++generation_;
    endMeasure(); backend_.releaseFrame();
    endDecode(); endScreen(); endFile();
    responseSize_ = responseAt_ = binaryBytes_ = binarySent_ = padding_ = screenAt_ = 0;
    binary_ = suffix_ = waitingOperation_ = waitingReading_ = waitingWaveform_ = waitingGenerator_ = screenBinary_ = maxPending_ = false;
    completion_ = Completion::None;
}
void Engine::reset() {
    ++generation_;
    endMeasure(); parser_.reset(); errors_.clear(); backend_.endSession(); endDecode(); endScreen(); endFile();
    responseSize_ = responseAt_ = binaryBytes_ = binarySent_ = padding_ = screenAt_ = 0;
    binary_ = suffix_ = waitingOperation_ = waitingReading_ = inputPending_ = maxPending_ = false;
    waitingWaveform_ = waitingGenerator_ = screenBinary_ = false;
    completion_ = Completion::None;
    waveStart_ = 1; waveStop_ = 0; wavePoints_ = 0; wavePeak_ = false; waveChannel_ = preambleChannels_ = 0; ese_ = sre_ = 0;
    profileKnown_ = false;
    generatorFaultsKnown_ = false; // A new session reports only the generator's stops from now on.
}
size_t Engine::available() const {
    return responseSize_ - responseAt_ + (binary_ ? binaryBytes_ - binarySent_ : 0) + (decodeBinary_ ? decodeTotal_ - decodeSent_ : 0) +
           (screenBinary_ ? ScreenBytes - screenAt_ : 0) + (fileBinary_ ? fileBytes_ - fileAt_ : 0) + padding_ + unsigned(suffix_);
}
size_t Engine::read(uint8_t *output, size_t capacity) {
    if (!output || !capacity) return 0;
    size_t used = 0;
    while (used < capacity && responseAt_ < responseSize_) output[used++] = response_[responseAt_++];
    // A definite block keeps its announced length: if its source goes while it is sent, zeros complete
    // it (padding_), the LF ends it and -230 is queued; the lease goes back once, the session stays.
    if (binary_ && used < capacity && responseAt_ == responseSize_ && binarySent_ < binaryBytes_) {
        const auto *source = backend_.frame(false);
        uint32_t scanned = 0;
        while (used < capacity && binarySent_ < binaryBytes_) {
            if (maxPending_) { output[used++] = uint8_t(pendingMax_ + 128); ++binarySent_; maxPending_ = false; continue; }
            if (pointAt_ == segments_[segmentAt_].count) { ++segmentAt_; pointAt_ = 0; continue; }
            if (scanned >= PeakScanBudget) break; // The rest on the next read: the main loop goes on.
            int16_t low = 0, high = 0;
            if (!source || !point(*source, segments_[segmentAt_], pointAt_, low, high, scanned)) {
                padding_ = binaryBytes_ - binarySent_; binarySent_ = binaryBytes_; maxPending_ = false;
                backend_.releaseFrame(); errors_.push(-230); break;
            }
            output[used++] = uint8_t(low + 128); ++binarySent_; ++pointAt_;
            if (peakStream_) { pendingMax_ = high; maxPending_ = true; }
        }
    }
    if (decodeBinary_ && used < capacity && responseAt_ == responseSize_) {
        const size_t want = decodeTotal_ - decodeSent_ < capacity - used ? decodeTotal_ - decodeSent_ : capacity - used;
        const size_t n = want ? backend_.decodeRead(decodeTicket_, output + used, want) : 0;
        if (want && !n) { padding_ = decodeTotal_ - decodeSent_; endDecode(); errors_.push(-230); } // Cancelled (Off, a new source).
        else {
            used += n; decodeSent_ += uint32_t(n);
            if (decodeSent_ == decodeTotal_ && decodeTicket_) { backend_.decodeEnd(decodeTicket_); decodeTicket_ = 0; }
        }
    }
    // DISP:DATA?: the leased BMP; a lease that went (expired and taken) pads the block like a lost frame.
    if (screenBinary_ && used < capacity && responseAt_ == responseSize_ && screenAt_ < ScreenBytes) {
        const size_t want = std::min<size_t>(ScreenBytes - screenAt_, capacity - used);
        const size_t n = screenTicket_ ? backend_.screenRead(screenTicket_, screenAt_, output + used, want) : 0;
        if (!n) { padding_ = ScreenBytes - screenAt_; screenAt_ = ScreenBytes; errors_.push(-230); }
        else { used += n; screenAt_ += uint32_t(n); }
        if (screenAt_ == ScreenBytes) endScreen(); // Every byte sent (or owed as padding): the buffer is free.
    }
    if (fileBinary_ && used < capacity && responseAt_ == responseSize_ && fileAt_ < fileBytes_) {
        const size_t n = backend_.fileRead(fileAt_, output + used, std::min<size_t>(fileBytes_ - fileAt_, capacity - used));
        if (!n) { padding_ = fileBytes_ - fileAt_; fileAt_ = fileBytes_; errors_.push(-250); } // Shrunk or unreadable.
        else { used += n; fileAt_ += uint32_t(n); }
    }
    while (used < capacity && padding_) { output[used++] = 0; --padding_; }
    if (used < capacity && suffix_ && !padding_ && (!binary_ || binarySent_ == binaryBytes_) && (!decodeBinary_ || decodeSent_ == decodeTotal_) &&
        (!screenBinary_ || screenAt_ == ScreenBytes) && (!fileBinary_ || fileAt_ == fileBytes_)) {
        output[used++] = '\n'; suffix_ = false; binary_ = false; decodeBinary_ = false; screenBinary_ = false;
        endFile();
    }
    if (!available()) responseSize_ = responseAt_ = 0;
    outputAt_ = now_; return used;
}
void Engine::poll(uint32_t now) {
    now_ = now;
    if (profileKnown_ && profile_ != backend_.multimeter()) { // A reply cut short says so (-410).
        const bool dropped = available() != 0;
        reset(); if (dropped) errors_.push(-410);
        return;
    }
    profile_ = backend_.multimeter(); profileKnown_ = true;
    // A stop the generator made by itself (DAC underrun, a setting above the clock's limit): -240 once
    // when the count moved (at most one a poll, never repeated for a stop already reported).
    const uint32_t faults = backend_.generator().faults;
    if (generatorFaultsKnown_ && faults != generatorFaults_) errors_.push(-240);
    generatorFaults_ = faults; generatorFaultsKnown_ = true;
    if (inputPending_ && uint32_t(now - inputAt_) >= 3000) {
        parser_.reset(); inputPending_ = false; errors_.push(-102);
    }
    if (available()) {
        if (uint32_t(now - outputAt_) >= 10000) { discardOutput(); errors_.push(-410); } // The output only: the session stays.
        return;
    }
    if (waitingDecode_) { // The CSV's size, a bounded slice a pass; then the definite block's header.
        const int known = backend_.decodeMeasure(decodeTicket_, decodeTotal_);
        if (!known && int32_t(now - deadline_) < 0) return;
        waitingDecode_ = false;
        if (known != 1) { endDecode(); error(-230, true); return; }
        char count[16]; std::snprintf(count, sizeof count, "%lu", (unsigned long)decodeTotal_);
        reply("#%u%s", unsigned(std::strlen(count)), count);
        decodeSent_ = 0; decodeBinary_ = true;
        if (!decodeTotal_) { backend_.decodeEnd(decodeTicket_); decodeTicket_ = 0; }
        return;
    }
    if (measuring_) { stepMeasure(); return; }
    if (waitingWaveform_) {
        Command command; std::snprintf(command.header,sizeof command.header,"WAV:%s",pendingWave_);
        command.query = true; waveformCommand(command); return;
    }
    if (waitingGenerator_) {
        const auto status = backend_.generator().status;
        if (status == DataStatus::Busy && int32_t(now - generatorDeadline_) < 0) return;
        waitingGenerator_ = false;
        if (status == DataStatus::Busy || status == DataStatus::Timeout || status == DataStatus::HardwareError) errors_.push(-240);
    }
    if (waitingOperation_) {
        if (backend_.busy() && int32_t(now - deadline_) < 0) return;
        waitingOperation_ = false;
        if (backend_.busy() || backend_.operationStatus() != DataStatus::Ok) errors_.push(-200);
    }
    if (waitingReading_) {
        if (backend_.meter().sequence == readingSequence_ && int32_t(now - deadline_) < 0) return;
        waitingReading_ = false;
        if (backend_.meter().sequence == readingSequence_) error(-230, true);
        else meterReading(false);
        return;
    }
    if (completion_ != Completion::None) {
        const bool pending = backend_.busy() || backend_.generator().status == DataStatus::Busy || (!backend_.multimeter() && !backend_.held() &&
            backend_.scopeRequest().trigger.mode == scope::TriggerMode::Single);
        if (pending && int32_t(now - deadline_) < 0) return;
        const auto action = completion_; completion_ = Completion::None;
        if (pending) { error(-230,action == Completion::Query); if (available()) return; }
        else if (action == Completion::Query) { reply("1"); return; }
        else if (action == Completion::Event) errors_.complete();
    }
    if (uint32_t(now - frameAt_) >= 10000) { backend_.releaseFrame(); preambleChannels_ = 0; }
    Command command;
    if (parser_.take(command)) dispatch(command);
}
void Engine::dispatch(const Command &c) {
    uint32_t head; std::memcpy(&head, c.header, sizeof head);
    usbtrace::mark(usbtrace::Tag::Command, head);
    if (c.error) { error(c.error, c.query); return; }
    const bool queryArgument = Header::matches(c.header, "WAVeform:MEASure") || Header::matches(c.header, "WAVeform:ENVelope") ||
                               Header::matches(c.header, "MMEMory:CATalog") ||
                               Header::matches(c.header, "MMEMory:DATA");
    if (c.query && *c.argument && !queryArgument) { error(-108,true); return; }
    if (common(c) || systemCommand(c) || generatorCommand(c) || decodeCommand(c)) return;
    if (backend_.multimeter() ? meterCommand(c) : scopeCommand(c)) return;
    error(-113, c.query);
}
void Engine::reply(const char *format, ...) {
    va_list args; va_start(args, format);
    const int length = std::vsnprintf(response_, sizeof response_, format, args); va_end(args);
    if (length < 0 || size_t(length) >= sizeof response_ - 1) { errors_.push(-200); return; }
    responseSize_ = size_t(length); responseAt_ = 0; binary_ = false; suffix_ = true; outputAt_ = now_;
}
void Engine::number(double value) {
    char text[56];
    if (Decimal::number(value,text,sizeof text)) reply("%s",text); else error(-230,true);
}
void Engine::error(int16_t code, bool query) { errors_.push(code); if (query) reply("NAN"); }
bool Engine::argument(const Command &c, double &value, Header::Unit unit) {
    if (!*c.argument) { error(-109, c.query); return false; }
    if (!Header::number(c.argument, value, unit)) { error(-104, c.query); return false; }
    return true;
}
bool Engine::boolean(const Command &c, bool &value) {
    if (!Header::boolean(c.argument, value)) { error(*c.argument ? -224 : -109, c.query); return false; }
    return true;
}
bool Engine::apply(const ScopeRequest &request, bool level) {
    backend_.releaseFrame();
    preambleChannels_ = 0;
    if (!(level ? backend_.setTrigger(request) : backend_.configureScope(request))) { error(-222); return false; }
    return true;
}
const scope::FrameView *Engine::frame(bool refresh) {
    const auto *value = backend_.frame(refresh); frameAt_ = now_;
    return value && value->valid() ? value : nullptr;
}
void Engine::waitOperation() { waitingOperation_ = backend_.busy(); deadline_ = now_ + OperationMs; }
void Engine::waitGenerator() {
    waitingGenerator_ = backend_.generator().status == DataStatus::Busy; generatorDeadline_ = now_ + GeneratorMs;
}
uint8_t Engine::statusByte() const {
    const uint8_t base = (available() ? 16 : 0) | (!errors_.empty() ? 4 : 0) | ((errors_.event() & ese_) ? 32 : 0);
    return base | ((base & sre_) ? 64 : 0);
}
} // namespace openosc::scpi
