#include "openosc/companion/link.hpp"
#if defined(__arm__)
#include "openosc/usb/instrument_usb.hpp"
#include "openosc/companion/wifi_power.hpp"
#include "openosc/companion/file_storage.hpp"
#include "openosc/scpi/engine.hpp"
#include <algorithm>
#include <new>

namespace openosc::companion {
namespace {
constexpr char SleepUnconfirmed[] = "Wireless module did not confirm sleep";
}

Link &link() {
#if OPENOSC_BOARD == 706
    // The external generator leaves room in SRAM4 for the wireless state.
    alignas(Link) static uint8_t storage[sizeof(Link)] __attribute__((section(".settings_workspace")));
    static auto *instance = new (storage) Link;
    return *instance;
#else
    static Link instance;
    return instance;
#endif
}

bool Link::exchangeDue(uint32_t now) const {
    const bool listening = (status_.ip && (status_.flags & Flag::Tcp)) || (status_.flags & Flag::Ble);
    return (listening || session_) && int32_t(now - exchangeAt_) >= 0;
}
bool Link::fileDue(uint32_t now) const {
    return ((status_.flags & Flag::FileRequest) || fileReply_.size) && int32_t(now - fileAt_) >= 0;
}
bool Link::workDue(uint32_t now) const {
    if (state_ == State::Off || state_ == State::Unavailable || state_ == State::Debug) return false;
    if (wakePending_ || pulsing_) return true;
    if (pending_ != Command{}) return transferActive_ || int32_t(now - nextTransfer_) >= 0;
    if (state_ == State::Starting) return int32_t(now - deadline_) >= 0;
    return state_ == State::Stopping || queuedCommand_ || int32_t(now - statusAt_) >= 0 ||
           (loadingNetworks_ && !(status_.flags & Flag::Scanning) && networkCount_ < status_.networks && networkCount_ < Networks) ||
           exchangeDue(now) || fileDue(now);
}
bool Link::deferCapture(uint32_t now) {
    if (state_ == State::Stopping) return true;
    return captureWindow_.defer(now, workDue(now), (session_ && !reject_) || (status_.flags & Flag::FileRequest));
}

bool Link::start() {
    if (state_ != State::Off && state_ != State::Unavailable && state_ != State::Debug) return false;
    if (state_ == State::Debug && deadline_) return false;
    state_ = State::Starting;
    notice_ = "Waking the companion";
    status_ = {};
    networkCount_ = 0;
    loadingNetworks_ = false;
    wakePending_ = true;
    ++revision_;
    return true;
}
bool Link::stop() {
    if (state_ != State::On && state_ != State::Starting) return false;
    state_ = State::Stopping;
    queuedCommand_ = false;
    ++revision_;
    return true;
}
bool Link::request(Command command, const uint8_t *data, size_t size) {
    if (state_ != State::On || queuedCommand_ || size > Payload ||
        (command == Command::PrepareSwd && !Uart::debugAllowed())) return false;
    queued_.command = command;
    queued_.size = size;
    if (size) std::memcpy(queued_.data, data, size);
    queuedCommand_ = true;
    if (command == Command::Scan) {
        networkCount_ = 0; loadingNetworks_ = true; ++revision_;
    }
    return true;
}
bool Link::join(unsigned index, const char *password) {
    const auto *target = network(index);
    const unsigned secret = std::strlen(password);
    if (!target || secret > PasswordBytes || (target->secure && secret < 8)) return false;
    const unsigned size = std::strlen(target->ssid);
    uint8_t data[SsidBytes + PasswordBytes + 1];
    data[0] = size;
    std::memcpy(data + 1, target->ssid, size);
    std::memcpy(data + 1 + size, password, secret);
    return request(Command::Join, data, size + secret + 1);
}
uint32_t Link::time(uint32_t now) const { return utc_ ? utc_ + uint32_t(now - utcAt_) / 1000 : 0; }

void Link::launch(Command command, const uint8_t *data, unsigned size, uint32_t now) {
    Frame request{++sequence_, command, uint8_t(size)};
    if (size) std::memcpy(request.data, data, size);
    outputSize_ = Wire::encode(request, output_);
    nextTransfer_ = now;
    pending_ = command;
    tries_ = 0;
    fetching_ = transferActive_ = false;
}
void Link::finish(InstrumentUsb &usb, State next, const char *notice) {
    captureWindow_ = {};
    releaseFileStorage();
    fileReply_ = {};
    if (session_ && !reject_) usb.releaseCompanionSerial();
    uart_.close();
    pending_ = {};
    state_ = next;
    if (next != State::Debug) status_ = {};
    queuedCommand_ = pulsing_ = wakePending_ = waitingForCapture_ = loadingNetworks_ = false;
    transferActive_ = fetching_ = false;
    session_ = credit_ = inputAt_ = inputSize_ = networkCount_ = 0;
    reject_ = false;
    notice_ = notice;
    ++revision_;
}
void Link::exchange(const Frame &frame, InstrumentUsb &usb) {
    if (frame.size < 3) return;
    const uint8_t session = frame.data[1];
    if (session_ != session) {
        if (session_ && !reject_) usb.releaseCompanionSerial();
        inputAt_ = inputSize_ = 0;
        session_ = session;
        reject_ = session && !usb.takeCompanionSerial();
    }
    credit_ = frame.data[2];
    if (!reject_ && session_ && frame.size > 3 && inputAt_ == inputSize_) {
        inputAt_ = 0;
        inputSize_ = frame.size - 3;
        std::memcpy(input_, frame.data + 3, inputSize_);
    }
}
void Link::accept(const Frame &frame, uint32_t now, InstrumentUsb &usb) {
    captureWindow_.release();
    pending_ = {};
    if (frame.command == Command::Sleep) {
        const bool sleeping = frame.data[0] == uint8_t(Result::Ok) || frame.data[0] == uint8_t(Result::Sleeping);
        finish(usb, sleeping ? State::Off : State::Unavailable, sleeping ? "Companion asleep" : SleepUnconfirmed);
        return;
    }
    if (frame.data[0] == uint8_t(Result::Sleeping)) { finish(usb, State::Off, "Companion asleep after inactivity"); return; }
    if (frame.data[0] != uint8_t(Result::Ok)) {
        if (frame.command == Command::Network || frame.command == Command::Scan) loadingNetworks_ = false;
        notice_ = frame.data[0] == uint8_t(Result::NoSavedNetwork)
            ? "Choose Find Wi-Fi networks to set up Station"
            : "Wireless module refused the request";
        ++revision_;
        return;
    }
    switch (frame.command) {
    case Command::Hello:
    case Command::Status: {
        if (frame.size < NetworkNameOffset || frame.data[1] > uint8_t(Mode::AccessPoint)) return;
        Status next{};
        next.mode = Mode(frame.data[1]); next.flags = frame.data[2]; next.idle = frame.data[3]; next.networks = frame.data[4];
        next.ip = get32(frame.data + 5); next.pin = get32(frame.data + 9);
        unsigned offset = NetworkNameOffset;
        if (frame.command == Command::Status && (status_.flags & next.flags & Flag::WifiSettings)) {
            if (offset == frame.size) return;
            next.wifiPower = frame.data[offset++];
            if (!wifiPowerValid(next.wifiPower)) return;
            if (next.mode == Mode::AccessPoint) {
                if (offset == frame.size) return;
                const unsigned length = frame.data[offset++];
                if (length < 8 || length > PasswordBytes || length > frame.size - offset) return;
                std::memcpy(next.apPassword, frame.data + offset, length);
                offset += length;
            }
        } else if (next.mode == Mode::AccessPoint && !(next.flags & Flag::WifiSettings)) {
            std::memcpy(next.apPassword, frame.data + LegacyApPasswordOffset, LegacyApPasswordBytes);
        }
        if (frame.size - offset > SsidBytes) return;
        std::memcpy(next.ssid, frame.data + offset, frame.size - offset);
        if (((next.flags ^ status_.flags) & ~Flag::FileRequest) || next.mode != status_.mode || next.ip != status_.ip ||
            next.idle != status_.idle || next.networks != status_.networks || next.wifiPower != status_.wifiPower ||
            std::strcmp(next.ssid, status_.ssid) || std::strcmp(next.apPassword, status_.apPassword)) ++revision_;
        status_ = next;
        if (frame.command == Command::Hello && state_ == State::Starting) {
            state_ = State::On; notice_ = nullptr; ++revision_;
        }
        if (loadingNetworks_ && !(status_.flags & Flag::Scanning) && networkCount_ >= status_.networks) {
            loadingNetworks_ = false; ++revision_;
        }
        break;
    }
    case Command::Network:
        if (frame.size >= 4 && unsigned(frame.size - 4) <= SsidBytes && frame.data[1] == networkCount_ && networkCount_ < Networks) {
            auto &network = networks_[networkCount_++];
            network = {};
            network.secure = frame.data[2]; network.signal = int8_t(frame.data[3]);
            std::memcpy(network.ssid, frame.data + 4, frame.size - 4);
            if (networkCount_ >= status_.networks || networkCount_ == Networks) loadingNetworks_ = false;
            ++revision_;
        }
        break;
    case Command::Scan:
        loadingNetworks_ = true; networkCount_ = 0; status_.flags |= Flag::Scanning;
        statusAt_ = now; ++revision_;
        break;
    case Command::Clock:
        if (frame.size == 5 && get32(frame.data + 1)) {
            utc_ = get32(frame.data + 1); utcAt_ = now; notice_ = "UTC synchronized";
        } else notice_ = "Waiting for network or browser time";
        ++revision_;
        break;
    case Command::Exchange: exchange(frame, usb); break;
    case Command::FileExchange:
        if (frame.size < 2 || !frame.data[1]) fileReply_.size = 0;
        else if (!fileReply_.size || fileReply_.data[0] != frame.data[1]) answerFileStorage(frame, fileReply_, now);
        break;
    case Command::PrepareSwd:
        if (state_ == State::On && frame.size == 1) {
            launch(Command::EnterSwd, nullptr, 0, now);
        }
        break;
    default:
        notice_ = nullptr; statusAt_ = now; ++revision_; break;
    }
}

void Link::serviceStream(uint32_t now, InstrumentUsb &usb, scpi::Engine &engine) {
    if (session_ && !reject_ && usb.cdcOpen()) {
        usb.releaseCompanionSerial(); reject_ = true;
        inputAt_ = inputSize_ = 0;
    }
    if (!session_ || reject_) return;
    while (inputAt_ != inputSize_ && engine.feed(input_[inputAt_], now)) ++inputAt_;
    engine.poll(now);
    engine.refill();
    // A command can reboot or power off through the shared backend. The ordinary controller
    // shutdown and recovery paths still own those operations.
}

void Link::serviceTransfer(uint32_t now, InstrumentUsb &usb) {
    if (transferActive_) {
        Frame reply;
        const auto result = uart_.poll(reply);
        if (result == Uart::Result::Busy) return;
        transferActive_ = false;
        if (result == Uart::Result::Fault) {
            finish(usb, State::Unavailable, "Companion DMA error");
            return;
        }
        if (result == Uart::Result::Done) {
            if (pending_ == Command::EnterSwd) {
                finish(usb, State::Debug, "Handing lines over to SWD");
                deadline_ = now + 5000;
                return;
            }
            if (!fetching_) {
                fetching_ = true;
                deadline_ = now + 900;
                nextTransfer_ = now;
            } else if (reply.size && reply.sequence == sequence_ && reply.command == pending_) {
                accept(reply, now, usb);
                return;
            } else nextTransfer_ = now + 8;
        }
    }
    if (fetching_ && int32_t(now - deadline_) >= 0) {
        if (++tries_ >= (pending_ == Command::Hello ? 3u : 2u)) {
            finish(usb, State::Unavailable, pending_ == Command::Sleep ? SleepUnconfirmed : "Companion link timed out");
            return;
        }
        fetching_ = false;
    }
    if (int32_t(now - nextTransfer_) < 0) return;
    if (fetching_) {
        uint8_t grant[Wire::Bytes];
        const auto size = Wire::encode({sequence_, Command::FetchReply}, grant);
        transferActive_ = uart_.start(grant, size, true);
    } else transferActive_ = uart_.start(output_, outputSize_);
}

void Link::poll(uint32_t now, bool busWindow, InstrumentUsb &usb, scpi::Engine &engine) {
    pollWire(now, busWindow, usb, engine);
    // The parser can start a capture and revoke a transfer just armed. Keep it after the UART.
    if (state_ == State::On) serviceStream(now, usb, engine);
}

void Link::pollWire(uint32_t now, bool busWindow, InstrumentUsb &usb, scpi::Engine &engine) {
    // Rebase regularly so the captured clock remains valid across the 32-bit uptime wrap.
    if (clockDue(now)) {
        const uint32_t elapsed = uint32_t(now - utcAt_) / 1000;
        utc_ += elapsed; utcAt_ += elapsed * 1000;
    }
    if (state_ == State::Debug) {
        if (deadline_ && uart_.probeOwnsClock()) {
            deadline_ = 0;
            notice_ = "SWD active on Wi-Fi / Bluetooth";
            ++revision_;
        } else if (deadline_ && int32_t(now - deadline_) >= 0) {
            deadline_ = 0;
            finish(usb, State::Unavailable, "SWD handoff timed out");
        }
        return;
    }
    if (!engaged()) return;
    if (pulsing_) {
        if (uart_.waking()) {
            if (int32_t(now - deadline_) < 0) return;
            uart_.wake(false);
            deadline_ = now + 85;
            captureWindow_.release();
        } else wakePending_ = true; // A capture took the bus before the wake pulse finished.
        pulsing_ = false;
        if (state_ == State::Stopping) { finish(usb, State::Off, "Companion off"); return; }
    }
    if (waitingForCapture_ == busWindow) {
        waitingForCapture_ = !busWindow;
        if (waitingForCapture_) blockedAt_ = now;
        else if (fetching_) deadline_ += now - blockedAt_;
        ++revision_;
    }
    if (!busWindow) return;
    if (wakePending_) {
        wakePending_ = false;
        if (state_ == State::Stopping) { finish(usb, State::Off, nullptr); return; }
        if (!uart_.open()) { finish(usb, State::Unavailable, "Companion pins or clock unavailable"); return; }
        uart_.wake(true);
        pulsing_ = true;
        deadline_ = now + 8;
        return;
    }
    if (pending_ != Command{}) {
        serviceTransfer(now, usb);
        return;
    }
    if (state_ == State::Starting) {
        if (int32_t(now - deadline_) >= 0) launch(Command::Hello, reinterpret_cast<const uint8_t *>("OSC1"), 4, now);
    } else if (state_ == State::Stopping) launch(Command::Sleep, nullptr, 0, now);
    else if (queuedCommand_) {
        launch(queued_.command, queued_.data, queued_.size, now); queuedCommand_ = false;
        std::memset(queued_.data, 0, sizeof queued_.data);
    } else if (int32_t(now - statusAt_) >= 0) {
        const uint8_t flags = (touched_ ? UserActivity : 0) |
                              ((status_.flags & Flag::WifiSettings) ? IncludeWifiSettings : 0);
        launch(Command::Status, &flags, 1, now); statusAt_ = now + 550; touched_ = false;
    } else if (loadingNetworks_ && !(status_.flags & Flag::Scanning) && networkCount_ < status_.networks && networkCount_ < Networks) {
        const uint8_t index = networkCount_;
        launch(Command::Network, &index, 1, now);
    } else if (fileDue(now) && (preferFiles_ || !exchangeDue(now))) {
        launch(Command::FileExchange, fileReply_.data, fileReply_.size, now);
        fileAt_ = now + 8;
        preferFiles_ = false;
    } else if (exchangeDue(now)) {
        preferFiles_ = true;
        uint8_t request[Payload]{session_, uint8_t(inputAt_ == inputSize_ ? Stream : 0), uint8_t(reject_)};
        unsigned bytes = 0;
        if (session_ && !reject_ && credit_ && engine.available()) bytes = engine.read(request + 3, std::min<unsigned>(Stream, credit_));
        launch(Command::Exchange, request, bytes + 3, now);
        exchangeAt_ = now + (session_ ? 8u : 100u);
    }
}
}
#endif
