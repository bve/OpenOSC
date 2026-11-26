#pragma once
#include "openosc/acquisition/frame.hpp"
#include "openosc/measurement/dmm/result.hpp"

namespace openosc {
class Csv {
public:
    static constexpr const char *DmmHeader =
        "timestamp_us,sequence,status,quantity,value_si,unit,display,packet_hold,user_hold,peer_relative\r\n";
    static constexpr const char *ScopeHeader = "index,time_seconds,ch1_code,ch2_code,ch1_volts,ch2_volts\r\n";
    static size_t dmm(const dmm::Result &result, bool userHold, char *output, size_t capacity);
    static const char *quantity(Quantity value); // The quantity column's word: "voltage", ... "unknown".
    static size_t scope(const scope::FrameView &frame, size_t index, char *output, size_t capacity);
    // The longest row scope() gives for any point of a record of these metadata, CRLF included, 0 for
    // metadata it cannot write: the widest index and time of the record (time's digits from the rate's
    // factors 2 and 5, else Decimal's 12), each recorded channel's widest code (4) and volts over its 256 codes.
    // The bound the waveform CSV's free-space check reserves a row (GitHub #164).
    static size_t scopeRowLimit(const scope::FrameMetadata &metadata);
    static size_t scopeMetadata(const scope::FrameMetadata &metadata, char *output, size_t capacity);
};
} // namespace openosc
