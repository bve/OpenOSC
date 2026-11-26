#pragma once
#include <cstdint>

namespace openosc::decoding {
// What the Scope's protocol decoder does (docs/SCOPE_PROTOCOL_DECODERS_PLAN.md). Off by default: the
// Scope is as it was. It is switched on by Analyze > Decoder > Protocol. `enabled` and `protocol` apart:
// F4 Scope leaves the decoder (enabled 0) and keeps the protocol, view and parameters for the next time.
// New protocols are appended: the IDs of the others never change.
enum class Protocol : uint8_t { Off, Uart, I2c, Spi, Lin, Can, Rs232, Rs485, Dmx512, Count };
enum class View : uint8_t { Annotations, Split, Table, Count };
// Complete: the record shown is decoded whole (the capture waits for it). Live: a bit snapshot, then
// the record released; only once its spike and memory owner are accepted (Protocols::liveAvailable).
enum class Update : uint8_t { Complete, Live, Count };
// The hysteresis around each line's threshold (plan §6.1). Auto: 10 % of that line's swing (its lowest to
// its highest canonical code, the CANH-CANL difference for the bus) in the very record decoded, measured
// by the job before decoding it. Manual: hysteresisMv. What a record was decoded with is in its ResultSet.
enum class Hysteresis : uint8_t { Auto, Manual, Count };
// The protocols' own parameters (plan §8), kept for every protocol so that switching between them
// loses nothing. The decoders' option structs are built from them for each record (DecodeJob).
struct UartSettings {
    uint32_t baud = 115200;
    uint8_t dataBits = 8, parity = 0, stopHalfBits = 2; // 5..9; None/Even/Odd/Mark/Space; 2/3/4 = 1/1.5/2 stop bits.
    uint8_t msbFirst = 0, inverted = 0;                 // inverted: the line idles low.
    int16_t rs232ThresholdMv = 0;                       // At the probe tip, for either line. Fits the existing padding.
    bool operator==(const UartSettings &o) const {
        return baud == o.baud && dataBits == o.dataBits && parity == o.parity && stopHalfBits == o.stopHalfBits &&
               msbFirst == o.msbFirst && inverted == o.inverted && rs232ThresholdMv == o.rs232ThresholdMv;
    }
};
static_assert(sizeof(UartSettings) == 12, "UART settings share the Preferences stack budget");
struct I2cSettings {
    uint8_t sdaChannel = 0; // SCL on the other channel.
    bool operator==(const I2cSettings &o) const { return sdaChannel == o.sdaChannel; }
};
struct SpiSettings {
    uint8_t clockChannel = 1, mode = 0, bits = 8, msbFirst = 1; // Data on the other channel; mode 0..3 (CPOL, CPHA).
    uint8_t manual = 0;           // 0: a word starts after a pause of gapNs; 1: from manualStart.
    uint32_t gapNs = 10000, manualStart = 0;
    bool operator==(const SpiSettings &o) const {
        return clockChannel == o.clockChannel && mode == o.mode && bits == o.bits && msbFirst == o.msbFirst &&
               manual == o.manual && gapNs == o.gapNs && manualStart == o.manualStart;
    }
};
struct LinSettings {
    static constexpr unsigned Ids = 64, UnknownLength = 0, MaximumLength = 8;
    uint32_t baud = 19200;
    uint8_t enhanced = 1;      // The checksum model (0x3C/0x3D always classic).
    uint8_t lengths[Ids / 2]{}; // Data bytes of each frame ID, a nibble each (0: unknown, raw to the next break).
    LinSettings() { length(0x3C, 8); length(0x3D, 8); }
    unsigned length(unsigned id) const { return id < Ids ? (lengths[id / 2] >> (4 * (id & 1))) & 15u : 0; }
    void length(unsigned id, unsigned bytes) {
        if (id >= Ids || bytes > MaximumLength) return;
        lengths[id / 2] = uint8_t((lengths[id / 2] & ~(15u << (4 * (id & 1)))) | bytes << (4 * (id & 1)));
    }
    bool operator==(const LinSettings &o) const;
};
// CAN on a transceiver's RXD (dominant low) of either channel, or on the bus itself: CANH (CH1) minus
// CANL (CH2), dominant where the difference is high, against its own threshold. One protocol for
// Classical CAN and ISO CAN FD frame by frame (FDF): the nominal rate and sample point for the
// arbitration phase and every Classical frame, the data ones for the data phase of an FD frame with
// BRS (docs/SCOPE_PROTOCOL_DECODERS_CAN_FD.md §4); dataBitrate >= bitrate.
enum class CanSource : uint8_t { RxdCh1, RxdCh2, Difference, Count };
struct CanSettings {
    uint32_t bitrate = 500000;
    uint16_t samplePointPermille = 750;
    CanSource source = CanSource::RxdCh1;
    int32_t differenceMv = 700; // ISO 11898-2: recessive below 0.5 V, dominant above 0.9 V.
    uint32_t dataBitrate = 2000000;
    uint16_t dataSamplePointPermille = 750;
    bool operator==(const CanSettings &o) const {
        return bitrate == o.bitrate && samplePointPermille == o.samplePointPermille && source == o.source &&
               differenceMv == o.differenceMv && dataBitrate == o.dataBitrate && dataSamplePointPermille == o.dataSamplePointPermille;
    }
};
// RS-485 and DMX512 (GitHub #163): Data+ on CH1 and Data- on CH2, decoded on CH1 - CH2 against its own
// threshold (the centre of the hysteresis band; near 0 V for any EIA-485 swing), or the logic output (RO)
// of an external receiver on one channel (`channel`, its thresholdMv). Normal: MARK (idle) where CH1 - CH2
// (or RO) is high; Inverted swaps the logical polarity, for a pair wired the other way round, without
// rewiring. RS-485 frames characters with UartSettings; DMX512 is fixed at 250 kbaud 8N2, LSB first.
enum class Rs485Source : uint8_t { Difference, Receiver, Count };
struct Rs485Settings {
    int32_t thresholdMv = 0; // CH1 - CH2 at the probe tips.
    Rs485Source source = Rs485Source::Difference;
    uint8_t inverted = 0;
    bool operator==(const Rs485Settings &o) const { return thresholdMv == o.thresholdMv && source == o.source && inverted == o.inverted; }
};
struct Settings {
    uint8_t enabled = 0;               // Boolean, kept beside the other byte-sized settings.
    Protocol protocol = Protocol::Off;  // Off by default; F4 keeps the last protocol (shown Off while not enabled).
    View view = View::Table;            // Decoder only at the first enable (plan §4.4); then the last chosen.
    Update update = Update::Complete;
    uint8_t channel = 0;               // The single-ended line: 0 CH1, 1 CH2.
    Hysteresis hysteresis = Hysteresis::Auto;
    int32_t thresholdMv[2] = {1650, 1650}; // At the probe tip, per channel.
    uint32_t hysteresisMv = 200;        // Manual: the total width around the threshold.
    uint32_t minPulseNs = 0;            // Anti-glitch: 0 off everywhere (plan §6.2).
    UartSettings uart;
    I2cSettings i2c;
    SpiSettings spi;
    LinSettings lin;
    CanSettings can;
    Rs485Settings rs485;
    bool rs485Line() const { return protocol == Protocol::Rs485 || protocol == Protocol::Dmx512; }
    bool uartInverted() const { return protocol == Protocol::Rs232 || (rs485Line() ? rs485.inverted : uart.inverted); }
    int32_t threshold(unsigned c) const { return protocol == Protocol::Rs232 ? uart.rs232ThresholdMv : thresholdMv[c]; }
    // CH1 - CH2 is decoded (the CAN bus or an RS-485 pair), against differenceThreshold().
    bool difference() const {
        return protocol == Protocol::Can ? can.source == CanSource::Difference : rs485Line() && rs485.source == Rs485Source::Difference;
    }
    int32_t differenceThreshold() const { return protocol == Protocol::Can ? can.differenceMv : rs485.thresholdMv; }
    void differenceThreshold(int32_t mv) { (protocol == Protocol::Can ? can.differenceMv : rs485.thresholdMv) = mv; }
    // Both source channels must be DC coupled (RS-485, DMX512): an AC-coupled level is never decoded.
    bool needsDc() const { return rs485Line(); }
    bool valid() const;
    // The parameters a result depends on (not enabled or view): a cached result is reused for the
    // same record only while this is unchanged.
    uint32_t hash() const;
    // The channels the protocol reads (a bit each); the threshold and its line of each.
    unsigned sources() const;
    bool operator==(const Settings &o) const;
    bool operator!=(const Settings &o) const { return !(*this == o); }
};
struct Protocols {
    static bool available(Protocol protocol) { return protocol != Protocol::Off && protocol < Protocol::Count; }
    // The protocols framed by UartSettings (baud, data bits, parity, stop bits, bit order) and UartDecoder.
    static bool uartFraming(Protocol protocol) { return protocol == Protocol::Uart || protocol == Protocol::Rs232 || protocol == Protocol::Rs485; }
    // The event family of a protocol (protocols::protocolOf of its events' kinds): 1 UART, RS-232 and
    // RS-485, 2 I2C, 3 SPI, 4 LIN, 5 CAN, 6 DMX512; 0 Off.
    static unsigned family(Protocol protocol);
    static bool liveAvailable() { return false; } // Until the snapshot spike and its owner are accepted.
    static const char *name(Protocol protocol);
    static const char *key(Protocol protocol); // settings.json value
    static const char *viewName(View view);
    static const char *viewKey(View view);
    static const char *updateKey(Update update);
    static constexpr int32_t ThresholdLimitMv = 400000; // 400 V at the probe tip.
    static constexpr int32_t Rs232ThresholdLimitMv = 25000;
    static constexpr uint32_t HysteresisLimitMv = 100000, MinPulseLimitNs = 1000000;
    static constexpr uint32_t MinimumBaud = 50, MaximumBaud = 20000000;      // UART, LIN
    static constexpr uint32_t MinimumBitrate = 5000, MaximumBitrate = 1000000; // CAN nominal (arbitration) phase
    static constexpr uint32_t MaximumDataBitrate = 10000000;                    // CAN FD data phase, >= the nominal
    static constexpr uint32_t GapLimitNs = 100000000;
    static constexpr unsigned AutoHysteresisPercent = 10; // Of the line's swing in the record (plan §6.1).
    // RS-232 (TIA-232-F): a receiver decides a mark below -3 V and a space above +3 V only. Its Auto
    // hysteresis is at least 3 V less the threshold's distance from 0 V: the band reaches halfway from a
    // 0 V threshold to either valid level (+-1.5 V) and never beyond them, so a floating or idle input's
    // noise around 0 V decodes as nothing (GitHub #164). Manual hysteresis and other protocols: 0.
    static constexpr int32_t Rs232ValidLevelMv = 3000;
    static uint32_t autoHysteresisFloorMv(Protocol protocol, int32_t thresholdMv);
    static const char *hysteresisKey(Hysteresis hysteresis); // settings.json / CSV: "auto", "manual"
};
} // namespace openosc::decoding
