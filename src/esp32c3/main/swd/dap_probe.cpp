#include "dap_probe.hpp"
#include "DAP_config.h"
#include "dap_limits.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>

namespace openosc::companion {
static_assert(DapProbe::PacketSize == DAP_PACKET_SIZE);
namespace {
constexpr uint32_t Ap = DAP_TRANSFER_APnDP, Read = DAP_TRANSFER_RnW;
constexpr uint32_t Csw = 0, Tar = 4, Drw = 12; // MEM-AP bank 0
constexpr uint32_t PowerRequest = 0x50000000, PowerAck = 0xa0000000;
constexpr uint32_t Cpuid = 0xe000ed00, Dhcsr = 0xe000edf0, Demcr = 0xe000edfc, FpCtrl = 0xe0002000;
constexpr uint32_t DebugKey = 0xa05f0000, DebugEnabled = 1, Halt = 2, MaskInterrupts = 8, Halted = 1u << 17;
constexpr uint32_t VectorCatch = 0x7f1;

bool transfer(uint32_t command, uint32_t &word) {
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        const uint8_t ack = SWD_Transfer(command, &word);
        if (ack != DAP_TRANSFER_WAIT) return ack == DAP_TRANSFER_OK;
        vTaskDelay(1);
    }
    return false;
}

bool write(uint32_t command, uint32_t word) { return transfer(command, word); }

// STM32H7 exposes the Cortex-M system control space through AHB-AP 0.
class DebugMemory {
public:
    bool attach() const {
        DAP_Setup();
        PORT_SWD_SETUP();
        constexpr uint8_t selectSwd[] = {0xff,0xff,0xff,0xff,0xff,0xff,0xff, 0x9e,0xe7,
                                         0xff,0xff,0xff,0xff,0xff,0xff,0xff, 0};
        SWJ_Sequence(sizeof selectSwd * 8, selectSwd);
        uint32_t id;
        if (!transfer(DP_IDCODE | Read, id) || !id || id == UINT32_MAX || !write(DP_ABORT, 0x1e) ||
            !write(DP_SELECT, 0) || !write(DP_CTRL_STAT, PowerRequest)) return false;
        for (unsigned attempt = 0; attempt < 25; ++attempt) {
            uint32_t status;
            if (!transfer(DP_CTRL_STAT | Read, status)) return false;
            if ((status & PowerAck) == PowerAck) {
                uint32_t csw;
                // Keep the AP's protection attributes. Use single, non-incrementing word accesses.
                return transfer(Ap | Csw | Read, csw) && transfer(DP_RDBUFF | Read, csw) &&
                       write(Ap | Csw, (csw & ~0x37u) | 0x80000002u);
            }
            vTaskDelay(1);
        }
        return false;
    }
    bool readWord(uint32_t address, uint32_t &word) const {
        return write(Ap | Tar, address) && transfer(Ap | Drw | Read, word) && transfer(DP_RDBUFF | Read, word);
    }
    bool writeWord(uint32_t address, uint32_t word) const {
        return write(Ap | Tar, address) && write(Ap | Drw, word) && transfer(DP_RDBUFF | Read, word);
    }
};

bool clearDebug(const DebugMemory &memory) {
    uint32_t cpu, status;
    if (!memory.attach() || !memory.readWord(Cpuid, cpu) || (cpu & 0xff00fff0) != 0x4100c270 ||
        !memory.readWord(Dhcsr, status)) return false;
    if (!(status & DebugEnabled)) return true;
    if (status & Halted) {
        // Disable the debugger's hardware breakpoints and vector catch before resuming.
        uint32_t demcr;
        if (!memory.writeWord(FpCtrl, 2) || !memory.readWord(Demcr, demcr) ||
            !memory.writeWord(Demcr, demcr & ~VectorCatch) ||
            ((status & MaskInterrupts) && !memory.writeWord(Dhcsr, DebugKey | DebugEnabled | Halt)) ||
            !memory.writeWord(Dhcsr, DebugKey | DebugEnabled)) return false;
        for (unsigned attempt = 0; attempt < 25 && (status & Halted); ++attempt) {
            vTaskDelay(1);
            if (!memory.readWord(Dhcsr, status)) return false;
        }
        if (status & Halted) return false;
    }
    return memory.writeWord(Dhcsr, DebugKey) && memory.readWord(Dhcsr, status) && !(status & DebugEnabled);
}
}

void DapProbe::reserve() {
    gpio_config_t pins{};
    pins.pin_bit_mask = (1ull << COMPANION_SWCLK) | (1ull << COMPANION_SWDIO);
    pins.mode = GPIO_MODE_INPUT;
    gpio_config(&pins);
    gpio_ll_matrix_out_default(&GPIO, COMPANION_SWDIO);
    gpio_pullup_en(gpio_num_t(COMPANION_SWDIO));
    PIN_SWCLK_TCK_SET();
    gpio_set_direction(gpio_num_t(COMPANION_SWCLK), GPIO_MODE_INPUT_OUTPUT);
    gpio_set_level(gpio_num_t(COMPANION_NRST), 1);
    pins.pin_bit_mask = 1ull << COMPANION_NRST;
    pins.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    pins.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&pins);
    DAP_Setup();
}

void DapProbe::disconnect() { DAP_Setup(); }

bool DapProbe::detachTarget() {
    if (!usedDebugPort_) return true;
    const bool released = clearDebug(DebugMemory{});
    disconnect();
    if (released) usedDebugPort_ = false;
    return released;
}

void DapProbe::release() {
    PORT_OFF();
    gpio_reset_pin(gpio_num_t(COMPANION_SWCLK));
    gpio_set_pull_mode(gpio_num_t(COMPANION_SWCLK), GPIO_PULLDOWN_ONLY);
    gpio_reset_pin(gpio_num_t(COMPANION_SWDIO));
    gpio_reset_pin(gpio_num_t(COMPANION_NRST));
}

size_t DapProbe::execute(uint8_t *request, size_t size, uint8_t *response) {
    if (!dapFits(request, size, PacketSize)) {
        response[0] = ID_DAP_Invalid;
        return 1;
    }
    if (request[0] == ID_DAP_SWJ_Clock) {
        const uint32_t clock = uint32_t(dap16(request + 1)) | uint32_t(dap16(request + 3)) << 16;
        if (clock) {
            const auto bounded = std::clamp<uint32_t>(clock, 50000, 2000000);
            for (unsigned i = 0; i < 4; ++i) request[1 + i] = bounded >> (8 * i);
        }
    }
    if (request[0] == ID_DAP_TransferConfigure) {
        // Bound pathological WAIT/match bursts so the network and watchdog can keep running.
        const auto retries = std::min<unsigned>(dap16(request + 2), 128);
        const auto matches = std::min<unsigned>(dap16(request + 4), 32);
        request[2] = retries; request[3] = 0;
        request[4] = matches; request[5] = 0;
    }
    const auto written = DAP_ExecuteCommand(request, response) & 0xffff;
    if ((request[0] == ID_DAP_Transfer || request[0] == ID_DAP_TransferBlock) && written >= 3 && response[1])
        usedDebugPort_ = true;
    PIN_SWCLK_TCK_SET();
    // Only single-command packets are accepted by this transport.
    if (request[0] == ID_DAP_Info && request[1] == DAP_ID_CAPABILITIES && written >= 3)
        response[2] &= ~(1u << 4);
    return written;
}
}
