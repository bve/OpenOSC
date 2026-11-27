#pragma once
#include "arm/DAP.h"

namespace openosc::companion {
inline uint16_t dap16(const uint8_t *bytes) {
    return uint16_t(bytes[0]) | uint16_t(bytes[1]) << 8;
}

// Arm's command engine assumes its input and output buffers already fit the command.
inline bool dapFits(const uint8_t *request, size_t size, size_t packet) {
    if (!size) return false;
    switch (request[0]) {
    case ID_DAP_Info: case ID_DAP_Connect: case ID_DAP_SWD_Configure: return size >= 2;
    case ID_DAP_Disconnect: case ID_DAP_ResetTarget: return true;
    case ID_DAP_HostStatus: case ID_DAP_Delay: return size >= 3;
    case ID_DAP_SWJ_Clock: return size >= 5;
    case ID_DAP_SWJ_Pins: return size >= 7;
    case ID_DAP_TransferConfigure: case ID_DAP_WriteABORT: return size >= 6;
    case ID_DAP_SWJ_Sequence:
        return size >= 2 && size >= 2 + (unsigned(request[1] ? request[1] : 256) + 7) / 8;
    case ID_DAP_SWD_Sequence: {
        if (size < 2) return false;
        size_t used = 2, reply = 2;
        for (unsigned i = 0; i < request[1]; ++i) {
            if (used >= size) return false;
            const uint8_t info = request[used++];
            const unsigned clocks = info & 63;
            const unsigned bytes = ((clocks ? clocks : 64) + 7) / 8;
            if (info & 128) reply += bytes;
            else used += bytes;
        }
        return used <= size && reply <= packet;
    }
    case ID_DAP_Transfer: {
        if (size < 3) return false;
        size_t used = 3, reply = 3;
        for (unsigned i = 0; i < request[2]; ++i) {
            if (used >= size) return false;
            const uint8_t control = request[used++];
            if ((control & (DAP_TRANSFER_RnW | DAP_TRANSFER_MATCH_VALUE)) == DAP_TRANSFER_RnW) reply += 4;
            else used += 4;
        }
        return used <= size && reply <= packet;
    }
    case ID_DAP_TransferBlock: {
        if (size < 5) return false;
        const size_t bytes = 4u * dap16(request + 2);
        return request[4] & DAP_TRANSFER_RnW ? 4 + bytes <= packet : 5 + bytes <= size;
    }
    default: return false;
    }
}
}
