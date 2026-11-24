#include "openosc/usb/tmc_pipe.hpp"
#include "openosc/usb/instrument_class.hpp"
#include "openosc/platform/critical_section.hpp"

namespace openosc::usb {
namespace {
Pipe &pipe() { return instrument.pipes[0]; }
}
bool TmcPipe::current(uint32_t generation) const {
    CriticalSection lock;
    return usb::current(0, generation);
}
bool TmcPipe::begin(uint32_t now, uint32_t generation, TmcRecovery::Port &port, uint32_t &epoch) {
    // A pass for an older session (a reset or new configuration since synchronize()) does
    // nothing: it would feed, dispatch or answer with the old Tmc on the new session's pipe.
    if (!current(generation)) return false;
    recovery_.poll(now, port, *this, generation);
    epoch = recovery_.epoch();
    if (!recovery_.holdsBulkOut()) tmc_.poll(now);
    return true;
}
void TmcPipe::service(uint32_t now, uint32_t generation, TmcRecovery::Port &port) {
    uint32_t epoch;
    if (begin(now, generation, port, epoch)) finish(now, generation, epoch);
}
void TmcPipe::dispatch(uint32_t now, uint32_t generation, TmcRecovery::Port &port) {
    uint32_t epoch;
    if (!begin(now, generation, port, epoch)) return;
    if (!recovery_.holdsBulkOut()) engine_.poll(now);
    else engine_.expire(now); // A failed recovery waits for a CLEAR: its output still times out.
    finish(now, generation, epoch);
    if (current(generation) && !recovery_.holdsBulkIn()) engine_.refill();
}
void TmcPipe::finish(uint32_t now, uint32_t generation, uint32_t epoch) {
    if (!current(generation)) return; // Reset during the dispatch.
    input(now, generation);
    if (tmc_.failed() && !recovery_.holdsBulkOut()) haltTmcOut(generation);
    output(generation);
    publish(epoch, generation);
}
void TmcPipe::input(uint32_t now, uint32_t generation) {
    // Held for a split: the buffer stays as received; never re-armed once an INITIATE was accepted.
    consume(0, generation, [&] { return !recovery_.holdsBulkOut(); }, [&](uint8_t byte) { return tmc_.feed(byte, now); });
}
void TmcPipe::output(uint32_t generation) {
    auto &p = pipe();
    if (!p.transmitBusy) {
        if (tmc_.needsZlp()) { // A short packet is never held back: it ends the host's transfer.
            CriticalSection lock; // Queued and no longer pending in one step, for EP0's D0.
            if (transmit(0, 0, generation)) tmc_.zlpSent();
            recovery_.shortPending(tmc_.needsZlp(), generation);
        } else if (!recovery_.holdsBulkIn()) {
            const size_t count = tmc_.packet(p.transmit, TransferBytes);
            if (count) {
                CriticalSection lock; // No INITIATE between this check and the queuing.
                // Only a CLEAR or ABORT_IN accepted meanwhile cancels the packet: its reply is
                // dropped anyway, so the data already read from the engine need not come back.
                // An ABORT_OUT leaves Bulk-IN alone: the packet is queued as generated.
                if (recovery_.holdsBulkIn()) tmc_.retract();
                // Never a reply with a hole: a packet the class refuses (bus reset or new
                // configuration under way) drops the whole TMC session instead.
                else if (!transmit(0, count, generation)) tmc_.reset();
                recovery_.shortPending(tmc_.needsZlp(), generation); // A final packet of 64-byte multiples needs one.
            }
        }
    }
}
void TmcPipe::publish(uint32_t epoch, uint32_t generation) {
    CriticalSection lock;
    if (recovery_.epoch() != epoch || recovery_.holdsBulkOut()) return; // EP0 answers from settled state.
    // The reply open in software; EP0 adds what the IN endpoint still holds, live from the
    // controller (Port::bulkInBusy), not this buffer's ownership.
    recovery_.publish({tmc_.inputTag(), tmc_.outputTag(), tmc_.bulkOutInProgress(), tmc_.bulkInInProgress()}, generation);
}
// The buffer we own (received, not re-armed) is dropped and re-armed; one still armed stays
// with the USB DMA (it holds no packet once the NAK was effective and no completion pending).
void TmcPipe::dropBulkOut() { if (pipe().receiveReady) receive(0); }
void TmcPipe::abortBulkOut() { tmc_.abortBulkOut(); dropBulkOut(); }
void TmcPipe::abortBulkIn() { tmc_.abortBulkIn(); }
void TmcPipe::clear() { tmc_.clear(); dropBulkOut(); }
bool TmcPipe::bulkInQueued() const { return pipe().transmitBusy || tmc_.needsZlp(); }
} // namespace openosc::usb
