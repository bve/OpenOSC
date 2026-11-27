#pragma once
#include "openosc/companion/capture_window.hpp"
#include "openosc/companion/protocol.hpp"
#include "openosc/companion/uart.hpp"

namespace openosc {
class InstrumentUsb;
namespace scpi { class Engine; }
namespace companion {
class Link {
public:
    enum class State { Off, Starting, On, Stopping, Unavailable, Debug };
    struct Network { char ssid[SsidBytes + 1]; int8_t signal; bool secure; };
    struct Status {
        Mode mode = Mode::Off;
        uint8_t flags = 0, idle = 0, networks = 0, wifiPower = 0;
        uint32_t ip = 0, pin = 0;
        char ssid[SsidBytes + 1]{}, apPassword[PasswordBytes + 1]{};
    };
    bool start();
    bool stop();
    bool request(Command command, const uint8_t *data = nullptr, size_t size = 0);
    bool set(Command command, uint8_t value) { return request(command, &value, 1); }
    bool join(unsigned network, const char *password);
    void activity() { touched_ = true; }
    void poll(uint32_t now, bool busWindow, InstrumentUsb &usb, scpi::Engine &engine);
    bool deferCapture(uint32_t now);
    State state() const { return state_; }
    const Status &status() const { return status_; }
    const Network *network(unsigned index) const { return index < networkCount_ ? networks_ + index : nullptr; }
    unsigned networkCount() const { return networkCount_; }
    bool scanning() const { return loadingNetworks_ || (status_.flags & Flag::Scanning); }
    uint32_t time(uint32_t now) const;
    unsigned revision() const { return revision_; }
    const char *notice() const { return notice_; }
    bool waitingForCapture() const { return waitingForCapture_; }
    bool active() const { return state_ == State::On || state_ == State::Starting; }
    bool engaged() const { return state_ != State::Off && state_ != State::Unavailable; }
    bool needsService(uint32_t now) const { return engaged() || clockDue(now); }
private:
    bool exchangeDue(uint32_t now) const;
    bool fileDue(uint32_t now) const;
    bool workDue(uint32_t now) const;
    void pollWire(uint32_t now, bool busWindow, InstrumentUsb &usb, scpi::Engine &engine);
    bool clockDue(uint32_t now) const { return utc_ && uint32_t(now - utcAt_) >= 3600000u; }
    void launch(Command command, const uint8_t *data, unsigned size, uint32_t now);
    void finish(InstrumentUsb &usb, State next, const char *notice);
    void accept(const Frame &frame, uint32_t now, InstrumentUsb &usb);
    void exchange(const Frame &frame, InstrumentUsb &usb);
    void serviceStream(uint32_t now, InstrumentUsb &usb, scpi::Engine &engine);
    void serviceTransfer(uint32_t now, InstrumentUsb &usb);
    Uart uart_;
    CaptureWindow captureWindow_;
    Status status_;
    Network networks_[Networks]{};
    Frame queued_{}, fileReply_{};
    uint32_t fileAt_ = 0;
    bool preferFiles_ = true;
    uint8_t output_[Wire::Bytes]{}, input_[Stream]{};
    State state_ = State::Off;
    Command pending_{};
    const char *notice_ = nullptr;
    uint32_t deadline_ = 0, nextTransfer_ = 0, blockedAt_ = 0, statusAt_ = 0, exchangeAt_ = 0, utc_ = 0, utcAt_ = 0;
    unsigned revision_ = 0, outputSize_ = 0, networkCount_ = 0;
    uint8_t sequence_ = 0, tries_ = 0, session_ = 0, credit_ = 0, inputSize_ = 0, inputAt_ = 0;
    bool pulsing_ = false, wakePending_ = false, waitingForCapture_ = false;
    bool transferActive_ = false, fetching_ = false;
    bool loadingNetworks_ = false, queuedCommand_ = false, reject_ = false, touched_ = false;
};
Link &link();
}
}
