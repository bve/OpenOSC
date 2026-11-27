#pragma once
#include "bridge.hpp"
#include "dap_probe.hpp"

namespace openosc::companion {
class SwdService {
public:
    SwdService(Bridge &bridge, uint32_t now) : bridge_(bridge), activeAt_(now) { dap_.reserve(); }
    ~SwdService() { dap_.release(); }
    SwdService(const SwdService &) = delete;
    SwdService &operator=(const SwdService &) = delete;
    bool poll(uint32_t now, uint32_t timeoutMs);
private:
    void resetSession(uint8_t session);
    void packet(uint32_t now);
    static constexpr size_t Header = 8, FrameSize = Header + DapProbe::PacketSize;
    Bridge &bridge_;
    DapProbe dap_;
    uint8_t input_[FrameSize]{}, output_[FrameSize]{};
    uint8_t session_ = 0;
    size_t received_ = 0, need_ = Header, sent_ = 0, pending_ = 0;
    uint32_t activeAt_, stopAt_ = 0, reconnectUntil_ = 0;
    bool attached_ = false, stopping_ = false;
};
}
