#pragma once
#include "openosc/measurement/dmm/meter_commands.hpp"
#include "openosc/platform/resources.hpp"
#include <atomic>

namespace openosc::dmm {
class UartDma final : public CommandPort {
public:
    explicit UartDma(PeripheralResources &resources) : resources_(resources) {}
    ~UartDma() { stop(); }
    bool start();
    bool stop();
    bool read(uint8_t &byte);
    bool send(CommandGroup group, uint32_t nowMs) override;
    void service(uint32_t nowMs);
    DataStatus status() const { return status_; }
    DataStatus txStatus() const override { return txStatus_; }
    uint32_t errors() const { return errors_; }
    uint32_t overflows() const { return overflows_; }
    static void uartInterrupt();
    static void dmaInterrupt();

private:
    void receive();
    void restartReceive();
    void interrupt();
    static UartDma *active_;
    PeripheralResources &resources_;
    PeripheralLease lease_;
    uint8_t queue_[256]{}, tx_[2]{};
    std::atomic<uint32_t> written_{0}, read_{0};
    volatile uint32_t errors_ = 0, overflows_ = 0;
    unsigned received_ = 0;
    volatile unsigned transmitted_ = 0;
    uint32_t txDeadline_ = 0;
    volatile DataStatus status_ = DataStatus::NoData, txStatus_ = DataStatus::NoData;
};
} // namespace openosc::dmm
