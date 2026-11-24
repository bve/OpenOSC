#pragma once
#include "openosc/scpi/engine.hpp"

namespace openosc::scpi {
// USBTMC 1.0 framing, independent of the USB controller and ISR context.
class Tmc {
public:
    explicit Tmc(Engine &engine) : engine_(engine) {}
    bool feed(uint8_t byte, uint32_t now);
    void poll(uint32_t now) { if (ending_ && engine_.endMessage(now)) ending_ = false; }
    size_t packet(uint8_t *output, size_t capacity); // Whole FS packets; final chunk includes padding.
    bool needsZlp() const { return zlp_; }
    void zlpSent() { zlp_ = false; active_ = false; ended_ = true; }
    bool failed() const { return failed_; }
    void reset();
    // USBTMC 1.0 section 4.2.1 state. bTags are those of the current or most recent transfer.
    uint8_t inputTag() const { return inputTag_; }
    uint8_t outputTag() const { return requestTag_; }
    bool bulkOutInProgress() const { return headerBytes_ || remaining_ || padding_ || ending_; }
    bool bulkInInProgress() const { return requested_ || active_; }
    uint32_t bulkOutBytes() const { return outData_; } // Message data bytes given to the engine.
    uint32_t bulkInBytes() const { return inData_; }   // Message data bytes queued in packets.
    // INITIATE_ABORT_BULK_OUT: the message being received is dropped (output stays).
    void abortBulkOut();
    // INITIATE_ABORT_BULK_IN: no further data for the transfer (packets already queued are
    // sent); a short packet ends it unless one already did or is pending.
    void abortBulkIn();
    // INITIATE_CLEAR: everything dropped; a reply started and not yet ended by a short packet is
    // ended with one (queued or already read: the host's drain read waits for it), as abortBulkIn.
    void clear();
    // The last packet() output was not queued (an abort or clear was accepted meanwhile): its
    // data is not counted and the transfer is not ended by it. Its bytes are not read again.
    void retract();
private:
    bool header();
    Engine &engine_;
    uint32_t generation_ = 0; // The engine's output this transfer reads (outputGeneration).
    uint8_t header_[12]{}, reply_[12]{};
    unsigned headerBytes_ = 0, replyAt_ = 12, padding_ = 0, outputPadding_ = 0;
    uint32_t remaining_ = 0, requested_ = 0, transfer_ = 0, total_ = 0, outData_ = 0, inData_ = 0;
    uint8_t inputTag_ = 0, requestTag_ = 0;
    bool end_ = false, ending_ = false, active_ = false, zlp_ = false, failed_ = false;
    bool ended_ = true; // The current Bulk-IN transfer has its short packet (or none started).
    struct Undo { uint32_t requested = 0, inData = 0; bool active = false, ended = true, zlp = false; } undo_;
};
} // namespace openosc::scpi
