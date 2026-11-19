#pragma once
#include <cstdint>

namespace openosc {
// DISP:DATA?'s claim on the preview buffer (screenshot::previewMemory), shared with the screenshot
// writer and the image viewer. A stream holds a ticket: busy() while it reads, and for IdleMs after
// its last read (a host gone without a session reset). After that another owner may take the buffer
// (revoke): the old ticket is then refused, so a late reader gets -230 padding, never another image.
class ScreenLease {
public:
    static constexpr uint32_t IdleMs = 15000;
    bool busy(uint32_t now) const { return ticket_ && int32_t(now - touchedAt_) < int32_t(IdleMs); }
    // A new lease (0: the buffer is still another stream's); any older ticket is void from now on.
    uint32_t take(uint32_t now) {
        if (busy(now)) return 0;
        if (!++generation_) ++generation_;
        ticket_ = generation_; touchedAt_ = now;
        return ticket_;
    }
    // A read under `ticket`: true while it is the lease, which it keeps alive.
    bool read(uint32_t ticket, uint32_t now) {
        if (!ticket || ticket != ticket_) return false;
        touchedAt_ = now;
        return true;
    }
    void end(uint32_t ticket) { if (ticket && ticket == ticket_) ticket_ = 0; }
    void revoke() { ticket_ = 0; } // A screenshot or an image takes the buffer.
private:
    uint32_t ticket_ = 0, generation_ = 0, touchedAt_ = 0;
};
} // namespace openosc
