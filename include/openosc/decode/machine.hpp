#pragma once
#include "openosc/decode/protocols/can_fd.hpp"
#include "openosc/decode/protocols/dmx.hpp"
#include "openosc/decode/protocols/i2c.hpp"
#include "openosc/decode/protocols/lin.hpp"
#include "openosc/decode/protocols/spi.hpp"
#include "openosc/decode/protocols/uart.hpp"
#include "openosc/decode/settings.hpp"
#include <new>
#include <type_traits>

namespace openosc::decoding {
// The one protocol state machine of a record (docs/SCOPE_PROTOCOL_DECODERS_PROTOCOLS.md §7.1): built in
// place in one storage as large as the largest of the six (CAN: the one automaton of Classical CAN and
// ISO CAN FD, CanFdDecoder), its options from Settings and the record
// (Fs, the two lines' uncertainty window). Lives in the decoder workspace with the job.
class ProtocolMachine {
public:
    // The settings' protocol for this record; false (nothing to decode) when its timing is outside the
    // decoder's range at this Fs (a bit of fewer than 2 or more than 2^24 points, a gap of no point).
    bool configure(const Settings &settings, const scope::FrameMetadata &metadata);
    ProtocolDecoder &decoder() { return *decoder_; }
    // The first clear level of a line of a two-line protocol (I2C, SPI), before its transitions.
    void initialLevel(uint8_t channel, int level);
    uint32_t dropped() const;   // Units lost for the pool.
    uint32_t overflows() const; // Bursts past the window of two lines (OVR).
    // UART autobaud's helpers (a hint only, docs/SCOPE_PROTOCOL_DECODERS_PROTOCOLS.md): built in the same
    // storage in place of a machine (one at a time; the next configure() builds the machine again).
    protocols::BaudEstimator &estimator() { return place<protocols::BaudEstimator>(); }
    protocols::BaudValidator &validator() { return place<protocols::BaudValidator>(); }
    // The window (points) in which edges of two lines have no known order: one point, plus the read
    // skew of the four DMA lanes in points of the record (plan §6.2).
    static uint32_t window(const scope::FrameMetadata &metadata);
private:
    template <class T> T &emplace() {
        static_assert(std::is_trivially_destructible<T>::value, "replaced in place, never destroyed");
        static_assert(sizeof(T) <= sizeof(storage_) && alignof(T) <= alignof(Storage), "fits the storage");
        auto *machine = new (storage_) T;
        decoder_ = machine;
        return *machine;
    }
    template <class T> T &place() {
        static_assert(std::is_trivially_destructible<T>::value && sizeof(T) <= sizeof(Storage), "fits the storage");
        decoder_ = nullptr; protocol_ = Protocol::Off;
        return *new (storage_) T;
    }
    template <class T> T &as() const { return *std::launder(reinterpret_cast<T *>(const_cast<unsigned char *>(storage_))); }
    union Storage {
        protocols::UartDecoder uart; protocols::I2cDecoder i2c; protocols::SpiDecoder spi;
        protocols::LinDecoder lin; protocols::CanFdDecoder can; protocols::DmxDecoder dmx;
        Storage() {}
    };
    alignas(Storage) unsigned char storage_[sizeof(Storage)];
    ProtocolDecoder *decoder_ = nullptr;
    Protocol protocol_ = Protocol::Off;
};
} // namespace openosc::decoding
