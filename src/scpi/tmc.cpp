#include "openosc/scpi/tmc.hpp"
#include <algorithm>
#include <cstring>

namespace openosc::scpi {
void Tmc::reset() {
    headerBytes_ = remaining_ = requested_ = transfer_ = total_ = padding_ = outputPadding_ = 0;
    outData_ = inData_ = 0;
    replyAt_ = 12; inputTag_ = requestTag_ = 0;
    end_ = ending_ = active_ = zlp_ = failed_ = false; ended_ = true; engine_.reset();
}
void Tmc::abortBulkOut() {
    headerBytes_ = remaining_ = padding_ = 0;
    end_ = ending_ = failed_ = false;
    engine_.abortInput();
}
void Tmc::abortBulkIn() {
    // A request waiting for its first packet, or a reply between packets, gets a short packet;
    // a reply whose short packet was queued (or is pending) is already terminated.
    const bool open = requested_ || active_;
    engine_.discardOutput();
    requested_ = transfer_ = outputPadding_ = 0;
    replyAt_ = 12;
    if (zlp_) return;
    if (open) { zlp_ = active_ = true; ended_ = false; }
}
void Tmc::clear() {
    // A reply started on the bus and not ended by a short packet (active_; ended_ is then false): the
    // host may be reading it (CHECK reported bmClear.D0 = 1) and only a short packet ends its read,
    // whether or not the queued packets were already read before this act (R1: a buffer-busy test
    // left that read to time out). A request with nothing sent yet, or an ended reply, gets none.
    const bool shortNeeded = active_;
    const uint8_t in = inputTag_, request = requestTag_;
    reset();
    inputTag_ = in; requestTag_ = request; // "Most recent transfer" for later INITIATE replies.
    if (shortNeeded) { zlp_ = active_ = true; ended_ = false; }
}
bool Tmc::header() {
    if (!header_[1] || uint8_t(header_[1] ^ header_[2]) != 255 || header_[3]) return false;
    const uint32_t size = uint32_t(header_[4]) | uint32_t(header_[5]) << 8 | uint32_t(header_[6]) << 16 | uint32_t(header_[7]) << 24;
    if (header_[0] == 1) {
        if (size > 1048576 || (header_[8] & ~1U) || header_[9] || header_[10] || header_[11]) return false;
        inputTag_ = header_[1]; remaining_ = size; padding_ = (4 - (size & 3)) & 3; outData_ = 0;
        end_ = header_[8] & 1; ending_ = !size && end_;
    } else if (header_[0] == 2) {
        // TransferSize is the host's 32-bit receive limit (USBTMC 1.0, Table 4).
        // packet() sends at most the available reply, including for ngscopeclient's large reads.
        // TermChar is ignored when TermCharEnabled is clear; Linux still sends '\n' there.
        if (requested_ || active_ || !size || header_[8] || header_[10] || header_[11]) return false;
        requestTag_ = header_[1]; requested_ = size; inData_ = 0; // NBYTES_TXD of this transfer.
    } else return false;
    headerBytes_ = 0; return true;
}
bool Tmc::feed(uint8_t byte, uint32_t now) {
    if (failed_) return true;
    if (ending_) { if (!engine_.endMessage(now)) return false; ending_ = false; }
    if (remaining_) {
        if (!engine_.feed(byte, now)) return false;
        ++outData_;
        --remaining_; if (!remaining_ && end_) ending_ = true;
    } else if (padding_) --padding_;
    else { header_[headerBytes_++] = byte; if (headerBytes_ == 12 && !header()) failed_ = true; }
    // EOM must also work when the transfer length is a multiple of four (no padding).
    if (ending_ && engine_.endMessage(now)) ending_ = false;
    return true;
}
void Tmc::retract() {
    requested_ = undo_.requested; inData_ = undo_.inData;
    active_ = undo_.active; ended_ = undo_.ended; zlp_ = undo_.zlp;
}
size_t Tmc::packet(uint8_t *output, size_t capacity) {
    if (!output || capacity < 64 || zlp_ || failed_) return 0;
    undo_ = {requested_, inData_, active_, ended_, zlp_};
    capacity &= ~size_t(63);
    if (!active_) {
        if (!requested_ || !engine_.available()) return 0;
        transfer_ = std::min<size_t>(requested_, engine_.available());
        std::memset(reply_, 0, sizeof reply_); reply_[0] = 2; reply_[1] = requestTag_; reply_[2] = ~requestTag_;
        for (unsigned i = 0; i < 4; ++i) reply_[4+i] = uint8_t(transfer_ >> (8*i));
        reply_[8] = transfer_ == engine_.available();
        outputPadding_ = (4 - (transfer_ & 3)) & 3; total_ = 12 + transfer_ + outputPadding_;
        replyAt_ = 0; requested_ = 0; active_ = true; ended_ = false; inData_ = 0;
    }
    size_t used = 0;
    while (replyAt_ < 12 && used < capacity) output[used++] = reply_[replyAt_++];
    while (transfer_ && used < capacity) { // A short packet ends the host's transfer: an early read stops only at 64.
        const size_t room = used & 63 ? 64 - (used & 63) : capacity - used; // An unfinished packet only to its end.
        const size_t count = engine_.read(output + used, std::min<size_t>(transfer_, room));
        transfer_ -= count; used += count; inData_ += count;
        if (!count || !(used & 63)) break;
    }
    while (!transfer_ && outputPadding_ && used < capacity) { output[used++] = 0; --outputPadding_; }
    if (!transfer_ && !outputPadding_) {
        zlp_ = !(total_ & 63); if (!zlp_) { active_ = false; ended_ = true; }
    }
    return used;
}
} // namespace openosc::scpi
