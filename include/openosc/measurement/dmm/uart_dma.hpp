#pragma once
#include "openosc/measurement/dmm/meter_commands.hpp"
#include "openosc/platform/resources.hpp"
#include <atomic>

namespace openosc::dmm {
// UART5, the DMM chip's link at 9600 8N1. The transmitter is interrupt-driven; the receiver either
// through DMA1 Stream0 under the DmmDma lease (Receive::Dma, the DMM), or one byte per RXNE interrupt
// with no stream and no lease (Receive::Interrupt, the Scope's DMM trace: the scope capture holds that
// stream, ScopeDma). Both fill the same queue that read() empties.
class UartDma final : public CommandPort {
public:
    enum class Receive : uint8_t { Dma, Interrupt };
    explicit UartDma(PeripheralResources &resources) : resources_(resources) {}
    ~UartDma() { stop(); }
    bool start(Receive receive = Receive::Dma); // Already running: true only in that mode and Ok.
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
    void push(uint8_t byte);
    void interrupt();
    static UartDma *active_;
    PeripheralResources &resources_;
    PeripheralLease lease_;
    uint8_t queue_[256]{}, tx_[2]{};
    std::atomic<uint32_t> written_{0}, read_{0};
    volatile uint32_t errors_ = 0, overflows_ = 0;
    bool running_ = false;
    Receive receive_ = Receive::Dma;
    unsigned received_ = 0;
    volatile unsigned transmitted_ = 0;
    uint32_t txDeadline_ = 0;
    volatile DataStatus status_ = DataStatus::NoData, txStatus_ = DataStatus::NoData;
};
} // namespace openosc::dmm
