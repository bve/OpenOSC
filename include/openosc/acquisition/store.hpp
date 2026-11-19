#pragma once
#include "openosc/acquisition/mailbox.hpp"
#include "openosc/acquisition/arena.hpp"
#include "openosc/platform/resources.hpp"
#include "openosc/acquisition/linear_copy_plan.hpp"

namespace openosc::scope {
// The frame readers see. A capture's record is adopted in place: the store takes over the arena
// region it lies in and gives it back only when the frame is dropped, so the capture can never
// overwrite a frame that is on show, in HOLD or being read. Roll snapshots are the one copy:
// DMA2 moves the chronological history into the arena's snapshot bank.
class FrameStore {
public:
    FrameStore(PeripheralResources &resources, AcquisitionArena &arena) : resources_(resources), arena_(arena) {}
    // Takes `frame`, which lies in arena `region`, as the latest frame. Needs no readers.
    bool adopt(const FrameView &frame, uint8_t region);
    // The foreground owner must keep this ring unchanged until busy()==false.
    bool beginLinear(const FrameView &source,uint32_t nowMs);
    void service(uint32_t nowMs);
    bool acquire(FrameLease &lease) { return mailbox_.acquire(lease); }
    // `consume` marks the frame seen; DataAccess's own trigger inspection does not count as seeing it.
    bool acquireLatest(FrameLease &lease, bool consume = true);
    // DataAccess's trigger inspection: the latest frame once per published frame, whatever readers
    // took it first (Leased, or released back to Idle). Does not mark it seen.
    bool acquireUninspected(FrameLease &lease);
    // A reader of DataAccess's own (the interleave offset measurement) that used the latest frame
    // `sequence` as a reader would: it is seen, so the capture may take its memory. Other frames: no.
    bool consume(uint32_t sequence) {
        if (!hasFrame_ || latest_.metadata().sequence != sequence) return false;
        consumed_ = true;
        return true;
    }
    // Gives the latest frame's memory back to the arena; refused while it is read.
    bool drop();
    // Hands the latest frame and its region to the caller, who now owns the region.
    bool detach(FrameView &frame, uint8_t &region);
    bool cancel() { return drop(); }
    uint32_t readers() const { return mailbox_.readers(); }
    bool busy() const { return copying_; }
    bool ready() const { return mailbox_.state() == FrameMailbox::State::Ready; }
    bool writable() const { return !copying_ && mailbox_.state() != FrameMailbox::State::Leased; }
    bool hasFrame() const { return hasFrame_; }
    uint32_t sequence() const { return hasFrame_ ? latest_.metadata().sequence : 0; } // The latest frame's.
    bool consumed() const { return consumed_; } // A reader has had the latest frame.
    uint8_t region() const { return hasFrame_ || copying_ ? region_ : 0; }
    DataStatus status() const { return status_; }
    static void interrupt();

private:
    bool halt();
    bool prepare(uint32_t now);
    bool startLinear();
    bool unpublish();
    static FrameStore *active_;
    PeripheralResources &resources_;
    AcquisitionArena &arena_;
    PeripheralLease peripheral_;
    FrameMailbox mailbox_;
    FrameView latest_;
    FrameView copySource_;
    LinearCopyPlan linearPlan_;
    uint32_t deadline_ = 0;
    volatile uint32_t flags_ = 0;
    uint8_t region_ = 0;
    bool copying_ = false, hasFrame_ = false, consumed_ = false, inspected_ = false;
    DataStatus status_ = DataStatus::NoData;
};
} // namespace openosc::scope
