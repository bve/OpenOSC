#include "openosc/decode/machine.hpp"
#include "openosc/acquisition/analysis.hpp"
#include <cmath>

namespace openosc::decoding {
using namespace protocols;
uint32_t ProtocolMachine::window(const scope::FrameMetadata &m) {
    const bool fourLanes = m.layout == scope::Layout::FourBanks || m.layout == scope::Layout::FourByteBanks ||
                           m.layout == scope::Layout::InterleavedCh1;
    if (!fourLanes || !m.samplesPerSecond.valid()) return 1;
    return 1 + uint32_t(std::ceil(scope::EdgeScanner::LaneSkewNs * 1e-9 * m.samplesPerSecond.value()));
}
bool ProtocolMachine::configure(const Settings &s, const scope::FrameMetadata &m) {
    protocol_ = s.protocol;
    const auto &fs = m.samplesPerSecond;
    switch (s.protocol) {
    case Protocol::Uart: case Protocol::Rs232: {
        UartOptions o;
        o.baud = s.uart.baud; o.inverted = s.uartInverted();
        o.format.dataBits = s.uart.dataBits; o.format.parity = Parity(s.uart.parity);
        o.format.stopHalfBits = s.uart.stopHalfBits; o.format.msbFirst = s.uart.msbFirst;
        o.timingUncertaintyPoints = window(m) - 1; // Four-lane read skew (EdgeScanner::LaneSkewNs); else 0.
        return emplace<UartDecoder>().configure(o, fs);
    }
    case Protocol::I2c: {
        I2cOptions o;
        o.sdaChannel = s.i2c.sdaChannel; o.sclChannel = uint8_t(s.i2c.sdaChannel ^ 1); o.windowPoints = window(m);
        return emplace<I2cDecoder>().configure(o);
    }
    case Protocol::Spi: {
        SpiOptions o;
        o.clockChannel = s.spi.clockChannel; o.dataChannel = uint8_t(s.spi.clockChannel ^ 1);
        o.cpol = s.spi.mode >> 1; o.cpha = s.spi.mode & 1; o.bits = s.spi.bits; o.msbFirst = s.spi.msbFirst;
        o.align = s.spi.manual ? SpiOptions::Align::Manual : SpiOptions::Align::Gap;
        o.gapNs = s.spi.gapNs; o.manualStart = s.spi.manualStart; o.windowPoints = window(m);
        return emplace<SpiDecoder>().configure(o, fs);
    }
    case Protocol::Lin: {
        LinOptions o;
        o.baud = s.lin.baud;
        o.checksum = s.lin.enhanced ? LinOptions::Checksum::Enhanced : LinOptions::Checksum::Classic;
        for (unsigned id = 0; id < LinSettings::Ids; ++id) o.lengths[id] = uint8_t(s.lin.length(id));
        return emplace<LinDecoder>().configure(o, fs);
    }
    case Protocol::Can: {
        static_assert(Protocols::MaximumDataBitrate == CanFdOptions::MaximumDataBitrate, "one data-rate limit");
        CanFdOptions o;
        o.bitrate = s.can.bitrate; o.samplePointPermille = s.can.samplePointPermille;
        o.dataBitrate = s.can.dataBitrate; o.dataSamplePointPermille = s.can.dataSamplePointPermille;
        o.dominantHigh = s.can.source == CanSource::Difference; // CANH - CANL is high when dominant; RXD low.
        return emplace<CanFdDecoder>().configure(o, fs);
    }
    default: decoder_ = nullptr; return false;
    }
}
void ProtocolMachine::initialLevel(uint8_t channel, int level) {
    if (protocol_ == Protocol::I2c) as<I2cDecoder>().initialLevel(channel, level);
    else if (protocol_ == Protocol::Spi) as<SpiDecoder>().initialLevel(channel, level);
}
uint32_t ProtocolMachine::dropped() const {
    switch (protocol_) {
    case Protocol::Uart: case Protocol::Rs232: return as<UartDecoder>().dropped();
    case Protocol::I2c: return as<I2cDecoder>().dropped();
    case Protocol::Spi: return as<SpiDecoder>().dropped();
    case Protocol::Lin: return as<LinDecoder>().dropped();
    case Protocol::Can: return as<CanFdDecoder>().dropped();
    default: return 0;
    }
}
uint32_t ProtocolMachine::overflows() const {
    return protocol_ == Protocol::I2c ? as<I2cDecoder>().windowOverflows()
           : protocol_ == Protocol::Spi ? as<SpiDecoder>().windowOverflows() : 0;
}
} // namespace openosc::decoding
