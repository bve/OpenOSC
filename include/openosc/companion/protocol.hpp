#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace openosc::companion {
inline constexpr unsigned Baud = 500000;
inline constexpr unsigned StopBits = 2, CharacterBits = 1 + 8 + StopBits;
inline constexpr unsigned Payload = 112, Stream = Payload - 3, Networks = 8;
inline constexpr unsigned SsidBytes = 32, PasswordBytes = 63;
inline constexpr unsigned ApNameBytes = sizeof("OpenOSC-XXXXXX") - 1;
inline constexpr char DefaultApPassword[] = "12345678";
inline constexpr unsigned LegacyApPasswordBytes = 12, LegacyApPasswordOffset = 13;
inline constexpr unsigned NetworkNameOffset = LegacyApPasswordOffset + LegacyApPasswordBytes;
inline constexpr uint8_t Version = 0xC2;
enum class Command : uint8_t {
    Hello = 1, Sleep, Status, Mode, Scan, Network, Join, Bluetooth, Tcp, Idle, Clock, Exchange, PrepareSwd, EnterSwd,
    FetchReply, WifiPower, FileExchange
};
enum class Result : uint8_t { Ok, Invalid, Failed, Sleeping, NoSavedNetwork };
enum class Mode : uint8_t { Off, Station, AccessPoint };
enum Flag : uint8_t { Connected = 1, Ble = 2, Tcp = 4, Scanning = 8, TimeValid = 16, Joining = 32, WifiSettings = 64, FileRequest = 128 };
enum StatusRequest : uint8_t { UserActivity = 1, IncludeWifiSettings = 128 };
// An extended status starts its variable part with power. In AP mode a length-prefixed
// password follows it. The network name still occupies the remainder of the status.
static_assert(NetworkNameOffset + 2 + PasswordBytes + ApNameBytes <= Payload);
inline constexpr uint8_t IdleMinutes[] = {0, 1, 5, 15, 30};
inline constexpr uint8_t DefaultIdleMinutes = 15;
inline bool idleValid(uint8_t value) {
    for (auto minutes : IdleMinutes) if (value == minutes) return true;
    return false;
}

struct Frame {
    uint8_t sequence = 0;
    Command command = Command::Hello;
    uint8_t size = 0;
    uint8_t data[Payload]{};
};
inline uint16_t checksum(const uint8_t *data, size_t bytes) {
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < bytes; ++i) {
        crc ^= uint16_t(data[i]) << 8;
        for (unsigned bit = 0; bit < 8; ++bit) crc = uint16_t((crc << 1) ^ (crc & 0x8000 ? 0x1021 : 0));
    }
    return crc;
}
inline void put32(uint8_t *out, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out[i] = uint8_t(value >> (8 * i));
}
inline uint32_t get32(const uint8_t *in) {
    return uint32_t(in[0]) | uint32_t(in[1]) << 8 | uint32_t(in[2]) << 16 | uint32_t(in[3]) << 24;
}

// Zero-delimited COBS, with a CRC covering the protocol version, sequence, command and payload.
class Wire {
public:
    static constexpr unsigned Overhead = 9, Bytes = Payload + Overhead;
    static size_t encode(const Frame &frame, uint8_t (&out)[Bytes]) {
        if (frame.size > Payload) return 0;
        uint8_t raw[Payload + 6]{Version, frame.sequence, uint8_t(frame.command), frame.size};
        std::memcpy(raw + 4, frame.data, frame.size);
        const auto crc = checksum(raw, 4 + frame.size);
        raw[4 + frame.size] = uint8_t(crc);
        raw[5 + frame.size] = uint8_t(crc >> 8);
        out[0] = 0; // Clear any truncated frame left at the receiver.
        size_t code = 1, used = 2;
        for (size_t i = 0; i < size_t(frame.size) + 6; ++i) {
            if (raw[i]) out[used++] = raw[i];
            else { out[code] = uint8_t(used - code); code = used++; }
        }
        out[code] = uint8_t(used - code);
        out[used++] = 0;
        return used;
    }

    void reset() { count_ = 0; invalid_ = false; }
    bool feed(uint8_t byte, Frame &frame) {
        if (byte) {
            if (count_ < sizeof input_) input_[count_++] = byte;
            else invalid_ = true;
            return false;
        }
        const bool valid = !invalid_ && decode(frame);
        reset();
        return valid;
    }

private:
    bool decode(Frame &frame) const {
        uint8_t raw[Payload + 6];
        unsigned read = 0, used = 0;
        while (read < count_) {
            const unsigned copy = unsigned(input_[read++]) - 1;
            if (copy > count_ - read || copy > sizeof raw - used) return false;
            std::memcpy(raw + used, input_ + read, copy);
            used += copy;
            read += copy;
            if (read < count_) {
                if (used == sizeof raw) return false;
                raw[used++] = 0;
            }
        }
        if (used < 6 || raw[0] != Version || raw[3] != used - 6 ||
            checksum(raw, used - 2) != (uint16_t(raw[used - 1]) << 8 | raw[used - 2])) return false;
        frame.sequence = raw[1];
        frame.command = Command(raw[2]);
        frame.size = raw[3];
        std::memcpy(frame.data, raw + 4, frame.size);
        return true;
    }
    uint8_t input_[Bytes - 2]{};
    unsigned count_ = 0;
    bool invalid_ = false;
};
} // namespace openosc::companion
