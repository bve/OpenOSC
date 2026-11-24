#pragma once
#include "openosc/scpi/tmc.hpp"
#include "openosc/usb/tmc_recovery.hpp"

namespace openosc::usb {
// The USBTMC bulk pipe (OUT 0x01, IN 0x81) between the class driver's buffers and scpi::Tmc,
// and the protocol side of its split transactions (TmcRecovery::Target). Main context only;
// no controller registers (those are behind TmcRecovery::Port).
// While TmcRecovery holds a direction, nothing is fed, dispatched or re-armed (OUT), or
// generated (IN); a short packet and the packet already in the USB DMA still go out. A packet
// generated while a CLEAR or ABORT_IN was accepted is retracted, not queued; one generated
// while an ABORT_OUT was accepted is queued (Bulk-IN is independent of it). A
// malformed USBTMC header halts Bulk-OUT only, so a CLEAR can still drain Bulk-IN.
class TmcPipe final : private TmcRecovery::Target {
public:
    TmcPipe(scpi::Tmc &tmc, scpi::Engine &engine, TmcRecovery &recovery) : tmc_(tmc), engine_(engine), recovery_(recovery) {}
    // service(): background pass (inside FAT waits), never the engine's commands; dispatch(): main poll.
    void service(uint32_t now, uint32_t generation, TmcRecovery::Port &port);
    void dispatch(uint32_t now, uint32_t generation, TmcRecovery::Port &port);

private:
    bool current(uint32_t generation) const;
    bool begin(uint32_t now, uint32_t generation, TmcRecovery::Port &port, uint32_t &epoch);
    void finish(uint32_t now, uint32_t generation, uint32_t epoch);
    void input(uint32_t now, uint32_t generation);
    void output(uint32_t generation);
    void publish(uint32_t epoch, uint32_t generation);
    void dropBulkOut();
    void abortBulkOut() override;
    void abortBulkIn() override;
    void clear() override;
    bool bulkInQueued() const override;
    bool shortPending() const override { return tmc_.needsZlp(); }
    uint32_t bulkOutBytes() const override { return tmc_.bulkOutBytes(); }
    uint32_t bulkInBytes() const override { return tmc_.bulkInBytes(); }
    scpi::Tmc &tmc_;
    scpi::Engine &engine_;
    TmcRecovery &recovery_;
};
} // namespace openosc::usb
