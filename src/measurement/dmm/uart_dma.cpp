#include "openosc/measurement/dmm/uart_dma.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/dma_stream.hpp"
#include "openosc/platform/interrupts.hpp"
#include <cstdint>

namespace openosc::dmm {
UartDma *UartDma::active_ = nullptr;
namespace {
alignas(32) uint8_t receiveBuffer[64] __attribute__((section(".dma")));
}
bool UartDma::start() {
    if (lease_.valid())
        return status_ == DataStatus::Ok;
    if (!resources_.acquire(PeripheralResources::DmmDma, lease_)) {
        status_ = DataStatus::Busy;
        return false;
    }
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_UART5_CLK_ENABLE();
    __HAL_RCC_AHBSRAM1_CLK_ENABLE();
    GPIO_InitTypeDef gpio{};
    gpio.Pin = GPIO_PIN_12 | GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF14_UART5;
    HAL_GPIO_Init(GPIOB, &gpio);
    RCC_PeriphCLKInitTypeDef clock{};
    clock.PeriphClockSelection = RCC_PERIPHCLK_UART5;
    clock.Usart234578ClockSelection = RCC_USART234578CLKSOURCE_D2PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) {
        lease_.release();
        status_ = DataStatus::HardwareError;
        return false;
    }
    UART5->CR1 = 0;
    UART5->CR2 = 0;
    UART5->CR3 = 0;
    UART5->PRESC = 0;
    UART5->BRR = (HAL_RCC_GetPCLK1Freq() + 4800) / 9600;
    UART5->ICR = 0xffffffff;
    UART5->RQR = USART_RQR_RXFRQ;
    DMA1_Stream0->CR &= ~DMA_SxCR_EN;
    const uint32_t started = Board::milliseconds();
    while (DMA1_Stream0->CR & DMA_SxCR_EN) {
        Board::service();
        if (Board::milliseconds() - started > 10) {
            lease_.release();
            status_ = DataStatus::Timeout;
            return false;
        }
    }
    DMA1->LIFCR = 0x3d;
    DMAMUX1_Channel0->CCR = DMA_REQUEST_UART5_RX;
    DMA1_Stream0->PAR = uint32_t(reinterpret_cast<uintptr_t>(&UART5->RDR));
    DMA1_Stream0->M0AR = uint32_t(reinterpret_cast<uintptr_t>(receiveBuffer));
    DMA1_Stream0->NDTR = 64;
    DMA1_Stream0->FCR = DMA_SxFCR_FEIE;
    DMA1_Stream0->CR = DmaStream::UartProtocol | (1U << 16) | DMA_SxCR_MINC | DMA_SxCR_CIRC | DMA_SxCR_TCIE | DMA_SxCR_HTIE |
                       DMA_SxCR_TEIE | DMA_SxCR_DMEIE;
    written_ = 0;
    read_ = 0;
    received_ = 0;
    errors_ = overflows_ = 0;
    active_ = this;
    Interrupts::attach(UART5_IRQn, uartInterrupt);
    Interrupts::attach(DMA1_Stream0_IRQn, dmaInterrupt);
    DMA1_Stream0->CR |= DMA_SxCR_EN;
    UART5->CR3 = USART_CR3_DMAR | USART_CR3_EIE;
    UART5->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_IDLEIE | USART_CR1_UE;
    const uint32_t ready = Board::milliseconds();
    while ((UART5->ISR & (USART_ISR_TEACK | USART_ISR_REACK)) != (USART_ISR_TEACK | USART_ISR_REACK)) {
        Board::service();
        if (Board::milliseconds() - ready > 10) {
            stop();
            status_ = DataStatus::Timeout;
            return false;
        }
    }
    status_ = DataStatus::Ok;
    txStatus_ = DataStatus::NoData;
    return true;
}
bool UartDma::stop() {
    if (!lease_.valid())
        return true;
    UART5->CR1 = 0;
    UART5->CR3 = 0;
    if (!DmaStream::disable(DMA1_Stream0)) {
        status_ = DataStatus::HardwareError;
        return false;
    }
    Interrupts::detach(UART5_IRQn);
    Interrupts::detach(DMA1_Stream0_IRQn);
    DMA1->LIFCR = 0x3d;
    __DSB();
    active_ = nullptr;
    lease_.release();
    status_ = DataStatus::NoData;
    txStatus_ = DataStatus::NoData;
    return true;
}
void UartDma::receive() {
    const unsigned current = (64 - DMA1_Stream0->NDTR) & 63;
    const volatile uint8_t *source = receiveBuffer;
    __DMB();
    while (received_ != current) {
        const uint8_t byte = source[received_];
        received_ = (received_ + 1) & 63;
        const uint32_t written = written_.load(std::memory_order_relaxed),
                       read = read_.load(std::memory_order_acquire);
        if (written - read >= sizeof queue_) {
            ++overflows_;
            continue;
        }
        queue_[written & 255] = byte;
        written_.store(written + 1, std::memory_order_release);
    }
}
void UartDma::restartReceive() {
    DMA1_Stream0->CR &= ~DMA_SxCR_EN;
    for (unsigned n = 0; n < 1000 && (DMA1_Stream0->CR & DMA_SxCR_EN); ++n) {}
    if (DMA1_Stream0->CR & DMA_SxCR_EN) return; // NDTR is locked: received_ still matches the buffer.
    DMA1->LIFCR = 0x3d;
    DMA1_Stream0->NDTR = 64;
    received_ = 0;
    DMA1_Stream0->CR |= DMA_SxCR_EN;
}
bool UartDma::read(uint8_t &byte) {
    const uint32_t read = read_.load(std::memory_order_relaxed);
    if (read == written_.load(std::memory_order_acquire))
        return false;
    byte = queue_[read & 255];
    read_.store(read + 1, std::memory_order_release);
    return true;
}
bool UartDma::send(CommandGroup group, uint32_t now) {
    if (status_ != DataStatus::Ok || txStatus_ == DataStatus::Busy || !Command::encode(group, tx_))
        return false;
    transmitted_ = 0;
    txDeadline_ = now + 100;
    txStatus_ = DataStatus::Busy;
    UART5->ICR = USART_ICR_TCCF;
    __DMB();
    UART5->CR1 |= USART_CR1_TXEIE_TXFNFIE;
    return true;
}
void UartDma::service(uint32_t now) {
    if (txStatus_ == DataStatus::Busy && int32_t(now - txDeadline_) >= 0) {
        UART5->CR1 &= ~(USART_CR1_TXEIE_TXFNFIE | USART_CR1_TCIE);
        txStatus_ = DataStatus::Timeout;
    }
}
void UartDma::interrupt() {
    const uint32_t status = UART5->ISR;
    if (status & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
        ++errors_;
        UART5->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
    }
    if (status & USART_ISR_IDLE) {
        UART5->ICR = USART_ICR_IDLECF;
        receive();
    }
    if ((UART5->CR1 & USART_CR1_TXEIE_TXFNFIE) && (status & USART_ISR_TXE_TXFNF)) {
        UART5->TDR = tx_[transmitted_++];
        if (transmitted_ == 2) {
            UART5->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
            UART5->CR1 |= USART_CR1_TCIE;
        }
    }
    if ((UART5->CR1 & USART_CR1_TCIE) && (UART5->ISR & USART_ISR_TC)) {
        UART5->ICR = USART_ICR_TCCF;
        UART5->CR1 &= ~USART_CR1_TCIE;
        txStatus_ = DataStatus::Ok;
    }
}
void UartDma::uartInterrupt() {
    if (active_)
        active_->interrupt();
}
// Stream 0's error flags as the vendor HAL treats them (HAL_DMA_IRQHandler, stm32h7xx_hal_dma.c): a
// FIFO error (FEIF0) or direct mode error (DMEIF0) is recorded and cleared and the stream runs on;
// only a transfer error (TEIF0) ends it. So FEIF/DMEIF on a running stream is a lost byte, counted
// as a UART line error is (DataAccess resets the packet framer): the link and its commands stay up
// (issue #23: it was HardwareError for good, every later command refused). TEIF, or a stream that
// no longer runs, restarts the reception.
void UartDma::dmaInterrupt() {
    if (!active_)
        return;
    static constexpr uint32_t Fifo = 1U << 0, DirectMode = 1U << 2, Transfer = 1U << 3;
    const uint32_t flags = DMA1->LISR;
    DMA1->LIFCR = 0x3d;
    if (flags & (Fifo | DirectMode | Transfer)) {
        ++active_->errors_;
        if ((flags & Transfer) || !(DMA1_Stream0->CR & DMA_SxCR_EN)) { // Stopped: receive again, as stock re-arms per packet.
            active_->restartReceive();
            return;
        }
    }
    active_->receive();
}
} // namespace openosc::dmm
