#include "swd_service.hpp"
#include "DAP_config.h"
#include "arm/DAP.h"
#include "esp_timer.h"

namespace openosc::companion {
namespace {
constexpr uint8_t Request = 1, Response = 2, Close = 3;
constexpr uint32_t Magic = 0x00504144; // OpenOCD CMSIS-DAP TCP signature.
constexpr uint32_t ReconnectMs = 5000;
constexpr uint32_t InterruptedReconnectMs = 60000;
}

void SwdService::resetSession(uint8_t session) {
    session_ = session;
    attached_ = false;
    cleanDisconnect_ = false;
    received_ = sent_ = pending_ = 0;
    need_ = Header;
    dap_.disconnect();
}

void SwdService::packet(uint32_t now) {
    put32(output_, Magic);
    output_[6] = Response;
    output_[7] = 0;
    size_t length = 0;
    if (input_[6] == Close) {
        stopping_ = dap_.detachTarget();
        if (stopping_) stopAt_ = uint32_t(esp_timer_get_time() / 1000) + 400;
        else {
            length = 1;
            output_[Header] = 1; // Detach failed. Keep the probe available to retry.
            activeAt_ = now;
        }
    } else {
        length = dap_.execute(input_ + Header, need_ - Header, output_ + Header);
        const auto command = output_[Header];
        if (command == ID_DAP_Connect && output_[Header + 1] != 0) {
            attached_ = true;
            cleanDisconnect_ = false;
            reconnectMs_ = 0;
        } else if (command == ID_DAP_Disconnect) {
            cleanDisconnect_ = true;
        }
    }
    output_[4] = length;
    output_[5] = length >> 8;
    pending_ = Header + length;
    sent_ = received_ = 0;
    need_ = Header;
    if (attached_) activeAt_ = now;
}

bool SwdService::poll(uint32_t now, uint32_t timeoutMs) {
    const auto session = bridge_.session();
    if (stopping_ && (!session || int32_t(now - stopAt_) >= 0)) return false;
    if (session != session_) {
        if (attached_) {
            disconnectedAt_ = now;
            reconnectMs_ = cleanDisconnect_ ? ReconnectMs : InterruptedReconnectMs;
        }
        resetSession(session);
    }
    const bool expired = reconnectMs_ ? now - disconnectedAt_ >= reconnectMs_
                                      : !attached_ && now - activeAt_ >= timeoutMs;
    if (expired) {
        // Drop an incomplete handshake before clearing the target's debug state.
        if (session_) bridge_.drop(session_);
        if (dap_.detachTarget()) return false;
        disconnectedAt_ = now;
        reconnectMs_ = ReconnectMs;
    }
    if (!session_) return true;
    if (sent_ < pending_) {
        sent_ += bridge_.write(session_, output_ + sent_, pending_ - sent_);
        return true;
    }
    if (stopping_) return true;
    received_ += bridge_.read(session_, input_ + received_, need_ - received_);
    if (received_ != need_) return true;
    if (need_ == Header) {
        const size_t length = size_t(input_[4]) | size_t(input_[5]) << 8;
        const bool dap = input_[6] == Request && length && length <= DapProbe::PacketSize;
        const bool close = input_[6] == Close && !length;
        if (get32(input_) != Magic || input_[7] || (!dap && !close)) {
            bridge_.drop(session_);
            return true;
        }
        need_ += length;
    }
    if (received_ == need_) packet(now);
    return true;
}
}
