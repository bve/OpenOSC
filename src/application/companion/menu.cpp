#include "openosc/companion/link.hpp"
#include "openosc/companion/wifi_power.hpp"
#if defined(__arm__)
#include "openosc/application/controller.hpp"
#include "openosc/companion/file_storage.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/usb/instrument_usb.hpp"
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <new>

namespace openosc {
namespace {
using companion::Command;
using companion::Flag;
using companion::Link;
using companion::Mode;
using ui::PropertyKind;

constexpr Mode WifiModes[] = {Mode::Off, Mode::AccessPoint, Mode::Station};

class CompanionRows final : public ui::PropertyList {
public:
    enum class Page { Main, Scan, Password };
    enum Row { Power, WirelessSwd, Wifi, TxPower, Networks, Tcp, Ble, Idle, Sync, Utc,
               Network, Address, ApPassword, Pin, MainCount };
    enum PasswordRow { Character, AddCharacter, DeleteCharacter, Connect, PasswordCount };

    const char *title() const override {
        return page == Page::Main ? "WIRELESS MODULE" : page == Page::Scan ? "WI-FI NETWORKS" : "WI-FI PASSWORD";
    }
    const char *notice() const override {
        if (page == Page::Password) return "Choose and add each character";
        if (page == Page::Scan) return link().scanning() ? "Scanning..." : "Select a network";
        if (link().notice()) return link().notice();
        return link().state() == Link::State::On ? nullptr : "Disconnect SWD debugger before enabling";
    }
    unsigned count() const override {
        if (page == Page::Main) return unsigned(MainCount) - unsigned(!showWirelessSwd_);
        return page == Page::Scan ? 1 + link().networkCount() : unsigned(PasswordCount);
    }
    Row mainRow(unsigned index) const {
        return Row(index + unsigned(!showWirelessSwd_ && index >= WirelessSwd));
    }
    unsigned mainIndex(Row row) const {
        return unsigned(row) - unsigned(!showWirelessSwd_ && row > WirelessSwd);
    }
    PropertyKind kind(unsigned row) const override {
        if (page == Page::Password) return row == Character ? PropertyKind::Number : PropertyKind::Action;
        if (page == Page::Scan) return PropertyKind::Action;
        const auto item = mainRow(row);
        if (item == Power) return PropertyKind::Action;
        if (link().state() != Link::State::On && item != Utc) return PropertyKind::Info;
        switch (item) {
        case Wifi: case Idle: return PropertyKind::Choice;
        case TxPower: return link().status().wifiPower ? PropertyKind::Choice : PropertyKind::Info;
        case Tcp: case Ble: return PropertyKind::Toggle;
        case WirelessSwd: case Networks: case Sync: return PropertyKind::Action;
        default: return PropertyKind::Info;
        }
    }
    void name(unsigned row, char *out, size_t size) const override {
        static constexpr const char *names[] = {"Power", "Wireless SWD", "Wi-Fi mode", "Wi-Fi TX power",
            "Find Wi-Fi networks", "Wi-Fi SCPI", "Bluetooth LE", "Sleep when idle", "Sync time", "UTC", "Network",
            "IPv4 address", "AP password", "BLE PIN"};
        const char *name = "";
        if (page == Page::Main) name = row < count() ? names[mainRow(row)] : "";
        else if (page == Page::Scan) {
            if (!row) name = "Scan again";
            else if (const auto *n = link().network(row - 1)) name = n->ssid;
        } else {
            static constexpr const char *actions[] = {"Character", "Add character", "Delete last", "Connect"};
            name = row < PasswordCount ? actions[row] : "";
        }
        std::snprintf(out, size, "%s", name);
    }
    void value(unsigned row, char *out, size_t size) const override {
        if (size) out[0] = '\0';
        if (page == Page::Scan) {
            if (const auto *n = row ? link().network(row - 1) : nullptr)
                std::snprintf(out, size, "%s %d dBm", n->secure ? "Secured" : "Open", int(n->signal));
            return;
        }
        if (page == Page::Password) {
            if (row == Character) std::snprintf(out, size, "%c", char(character));
            if (row == AddCharacter) std::snprintf(out, size, "%u chars", unsigned(length));
            return;
        }
        const auto &status = link().status();
        const auto item = mainRow(row);
        if (item == Power) {
            static constexpr const char *states[] = {"Off", "Starting", "On", "Stopping", "No reply", "SWD"};
            std::snprintf(out, size, "%s", link().waitingForCapture() ? "Waiting for capture" : states[unsigned(link().state())]);
        } else if (item == Utc) {
            const uint32_t utc = link().time(now);
            if (utc) std::snprintf(out, size, "%02lu:%02lu:%02lu", (unsigned long)(utc / 3600 % 24),
                                  (unsigned long)(utc / 60 % 60), (unsigned long)(utc % 60));
            else std::snprintf(out, size, "Unsynced");
        } else if (link().state() == Link::State::On || link().state() == Link::State::Debug) {
            switch (item) {
            case Network: std::snprintf(out, size, "%s", status.ssid); break;
            case Address:
                if (status.ip) std::snprintf(out, size, "%u.%u.%u.%u", unsigned(status.ip >> 24),
                    unsigned(status.ip >> 16 & 255), unsigned(status.ip >> 8 & 255), unsigned(status.ip & 255));
                else std::snprintf(out, size, "%s", status.flags & Flag::Joining ? "Connecting" : "Offline");
                break;
            case ApPassword: std::snprintf(out, size, "%s", status.apPassword); break;
            case TxPower:
                if (!(status.flags & Flag::WifiSettings)) std::snprintf(out, size, "Update ESP");
                else PropertyList::value(row, out, size);
                break;
            case Pin:
                if (status.flags & Flag::Ble) std::snprintf(out, size, "%06lu", (unsigned long)status.pin);
                break;
            default: PropertyList::value(row, out, size); break;
            }
        }
    }
    unsigned options(unsigned row) const override {
        if (page != Page::Main || link().state() != Link::State::On) return 0;
        const auto item = mainRow(row);
        if (item == TxPower) return link().status().wifiPower ? companion::WifiPowerCount : 0;
        return item == Wifi ? std::size(WifiModes) : item == Idle ? std::size(companion::IdleMinutes) : item == Tcp || item == Ble ? 2 : 0;
    }
    unsigned option(unsigned row) const override {
        const auto &status = link().status();
        const auto item = mainRow(row);
        if (item == Wifi) return unsigned(std::find(std::begin(WifiModes), std::end(WifiModes), status.mode) - std::begin(WifiModes));
        if (item == TxPower) return companion::wifiPowerIndex(status.wifiPower);
        if (item == Ble) return bool(status.flags & Flag::Ble);
        if (item == Tcp) return bool(status.flags & Flag::Tcp);
        if (item == Idle) for (unsigned i = 0; i < options(row); ++i) if (companion::IdleMinutes[i] == status.idle) return i;
        return 0;
    }
    void optionName(unsigned row, unsigned index, char *out, size_t size) const override {
        const auto item = mainRow(row);
        if (item == Wifi) {
            static constexpr const char *modes[] = {"Off", "Station", "Access point"};
            std::snprintf(out, size, "%s", index < std::size(WifiModes) ? modes[unsigned(WifiModes[index])] : "");
        } else if (item == TxPower && index < companion::WifiPowerCount) {
            std::snprintf(out, size, "%s dBm", companion::WifiPowerLevels[index].dbm);
        } else if (item == Idle && index < options(row)) {
            if (!index) std::snprintf(out, size, "Never");
            else std::snprintf(out, size, "%u min", unsigned(companion::IdleMinutes[index]));
        } else std::snprintf(out, size, "%s", index ? "On" : "Off");
    }
    bool choose(unsigned row, unsigned index) override {
        if (index >= options(row)) return false;
        switch (mainRow(row)) {
        case Wifi: return link().set(Command::Mode, uint8_t(WifiModes[index]));
        case Idle: return link().set(Command::Idle, companion::IdleMinutes[index]);
        case TxPower: return link().set(Command::WifiPower, companion::WifiPowerLevels[index].quarters);
        case Tcp: return link().set(Command::Tcp, index);
        case Ble: return link().set(Command::Bluetooth, index);
        default: return false;
        }
    }
    int32_t number(unsigned) const override { return character; }
    void setNumber(unsigned, int32_t value) override { character = std::clamp<int32_t>(value, 32, 126); }
    void adjust(unsigned row, int steps) override { setNumber(row, character + steps); }
    int coarseStep(unsigned) const override { return 10; }
    void clearPassword() { std::memset(password, 0, sizeof password); length = 0; character = 'a'; }
    static Link &link() { return companion::link(); }
    uint32_t now = 0;
    Page page = Page::Main;
    unsigned chosenNetwork = 0;
    int32_t character = 'a';
    unsigned rendered = 0;
    char password[companion::PasswordBytes + 1]{};
    uint8_t length = 0;

private:
    const bool showWirelessSwd_ = companion::Uart::debugAllowed();
};
CompanionRows &rows() {
    alignas(CompanionRows) static uint8_t storage[sizeof(CompanionRows)];
    static auto *menu = new (storage) CompanionRows;
    return *menu;
}
}

void ApplicationController::openCompanion() {
    auto &menu = rows();
    menu.page = CompanionRows::Page::Main;
    menu.clearPassword();
    menu.now = Board::milliseconds();
    menu.rendered = companion::link().revision();
    state_.settingsOpen = false;
    openPanel(Panel::Companion, menu);
}
void ApplicationController::companionKeys(uint32_t keys, uint32_t now) {
    using Menu = CompanionRows;
    using Page = Menu::Page;
    using Outcome = ui::PropertyNavigator::Outcome;
    if (!keys) return;
    auto &link = companion::link();
    link.activity();
    auto &menu = rows();
    const auto result = navigator_.keys(keys, menu);
    if (result == Outcome::Back) {
        menu.clearPassword();
        if (menu.page == Page::Main) {
            panel_ = Panel::None;
            state_.settingsOpen = true;
            navigator_.reset(settingsMenu_.selected());
            showSettings();
            return;
        }
        menu.page = menu.page == Page::Password ? Page::Scan : Page::Main;
        navigator_.reset(menu.page == Page::Scan ? menu.chosenNetwork + 1 : menu.mainIndex(Menu::Networks));
    } else if (result == Outcome::Activate) {
        const unsigned row = navigator_.row();
        bool accepted = true;
        if (menu.page == Page::Main) {
            const auto item = menu.mainRow(row);
            if (item == Menu::Power) accepted = link.active() ? link.stop() : link.start();
            if (item == Menu::WirelessSwd) accepted = link.request(Command::PrepareSwd);
            if (item == Menu::Networks) {
                accepted = link.request(Command::Scan);
                if (accepted) { menu.page = Page::Scan; navigator_.reset(); }
            }
            if (item == Menu::Sync) accepted = link.request(Command::Clock);
        } else if (menu.page == Page::Scan) {
            if (!row) accepted = link.request(Command::Scan);
            else if (const auto *network = link.network(row - 1)) {
                menu.chosenNetwork = row - 1;
                if (network->secure) {
                    menu.clearPassword(); menu.page = Page::Password; navigator_.reset();
                } else {
                    accepted = link.join(menu.chosenNetwork, "");
                    if (accepted) { menu.page = Page::Main; navigator_.reset(menu.mainIndex(Menu::Networks)); }
                }
            }
        } else if (row == Menu::AddCharacter) {
            if (menu.length < companion::PasswordBytes) {
                menu.password[menu.length++] = char(menu.character);
                menu.password[menu.length] = 0;
            }
        } else if (row == Menu::DeleteCharacter) {
            if (menu.length) menu.password[--menu.length] = 0;
        } else if (row == Menu::Connect) {
            accepted = link.join(menu.chosenNetwork, menu.password);
            if (accepted) {
                menu.clearPassword(); menu.page = Page::Main; navigator_.reset(menu.mainIndex(Menu::Networks));
            } else if (menu.length < 8) {
                view_.notify(Tooltip::Unavailable, "Use at least 8 characters", now);
                return;
            }
        }
        if (!accepted) view_.notify(Tooltip::Unavailable, "Companion command unavailable", now);
    } else if (result == Outcome::Refused) view_.notify(Tooltip::Unavailable, "Companion command unavailable", now);
    view_.properties(menu, navigator_);
}
void ApplicationController::serviceCompanion(uint32_t now, scpi::Engine &engine) {
    auto &link = companion::link();
    if (panel_ != Panel::Companion && !link.needsService(now)) return;
    const bool storageReady = uiReady_ && fs_.mounted() && shutdown_ == Shutdown::None && !recoveryPending_;
    companion::serviceFileStorage(now, storageReady, storageReady && !shots_.busy() &&
        !data_.scopeRecording() && !data_.meterLogger().active() && !data_.meterSession().active() &&
        !imageReader_.busy() && panel_ != Panel::Images && !capturesBrowsing() && !sessionsBrowsing() && !awgFiles_);
    if (shutdown_ != Shutdown::None || recoveryPending_) link.stop();
    auto &menu = rows();
    const auto previous = link.time(menu.now);
    link.poll(now, link.engaged() && data_.companionWindow(link.state() == companion::Link::State::Stopping), *usb_, engine);
    const bool nextSecond = link.time(now) != previous;
    menu.now = now;
    if (panel_ != Panel::Companion && menu.length) menu.clearPassword();
    if (panel_ == Panel::Companion && (menu.rendered != link.revision() ||
        (nextSecond && menu.page == CompanionRows::Page::Main && navigator_.mode() == ui::PropertyNavigator::Mode::Browse))) {
        if (navigator_.row() >= menu.count()) navigator_.reset();
        menu.rendered = link.revision();
        view_.properties(menu, navigator_);
    }
}
}
#endif
