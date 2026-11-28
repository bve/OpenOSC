#include "bluetooth.hpp"
#include "network.hpp"
#include "portal.hpp"
#include "openosc/companion/protocol.hpp"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "ota/boot.hpp"
#include "ota/web_network.hpp"
#include "sleep_timer.hpp"
#include "watchdog.hpp"
#include "swd/pins.h"
#include "swd/swd_service.hpp"
#include <cstdio>
#include "freertos/task.h"

namespace openosc::companion {
namespace {
constexpr gpio_num_t Receive = gpio_num_t(COMPANION_SWCLK), Transmit = gpio_num_t(COMPANION_SWDIO);
constexpr gpio_num_t Recovery = gpio_num_t(COMPANION_RECOVERY);
constexpr uart_port_t Uart = UART_NUM_1;
// Keep the web admin reachable after a handoff while the STM32 reconnects.
constexpr uint32_t ReconnectGraceMs = 120000;
uint32_t milliseconds() { return esp_timer_get_time() / 1000; }
[[noreturn]] void sleep() {
    gpio_reset_pin(Transmit);
    gpio_set_direction(Transmit, GPIO_MODE_INPUT);
    gpio_reset_pin(Receive);
    gpio_set_direction(Receive, GPIO_MODE_INPUT);
    gpio_set_pull_mode(Receive, GPIO_PULLDOWN_ONLY);
    for (unsigned i = 0; i < 25 && gpio_get_level(Receive); ++i) vTaskDelay(1);
    if (gpio_get_level(Receive)) {
        // Avoid repeatedly waking on a held-high SWCLK.
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(2000000));
    } else {
        ESP_ERROR_CHECK(esp_deep_sleep_enable_gpio_wakeup(1ull << Receive, ESP_GPIO_WAKEUP_GPIO_HIGH));
    }
    esp_deep_sleep_start();
}

bool recoveryRequested() {
    gpio_set_direction(Recovery, GPIO_MODE_INPUT);
    gpio_set_pull_mode(Recovery, GPIO_PULLUP_ONLY);
    // Physically pull GPIO5 low during an ESP reset to enable a standalone probe.
    vTaskDelay(10);
    return !gpio_get_level(Recovery);
}

void startUart() {
    uart_config_t config{};
    config.baud_rate = Baud;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_2;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_param_config(Uart, &config));
    ESP_ERROR_CHECK(uart_set_pin(Uart, UART_PIN_NO_CHANGE, Receive, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_set_line_inverse(Uart, UART_SIGNAL_RXD_INV));
    gpio_set_pull_mode(Receive, GPIO_PULLDOWN_ONLY);
    ESP_ERROR_CHECK(uart_driver_install(Uart, 512, 0, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_set_rx_timeout(Uart, 2));
}

class Controller {
public:
    [[noreturn]] void standalone() {
        if (!network_.initialize()) sleep();
        const bool wifi = network_.mode(Mode::AccessPoint, milliseconds());
        const bool ble = bluetooth_.enable(true, network_.name(), network_.passkey());
        if (!wifi && !ble) sleep();
        std::printf("Wireless SWD recovery mode\nAP: %s  Wi-Fi password: %s\nBLE PIN: %06lu\n",
                    network_.name(), network_.apPassword(), (unsigned long)network_.passkey());
        std::fflush(stdout);
        probe();
        bluetooth_.shutdown();
        network_.shutdown();
        sleep();
    }
    [[noreturn]] void run(Mode resume) {
        const auto boot = milliseconds();
        uint32_t reconnectAt = boot;
        uint32_t bridgeActivity = bridge_.activity();
        SleepTimer sleepTimer(boot);
        bool initialized = resume != Mode::Off;
        bool linked = false;
        if (initialized) {
            if (!network_.initialize()) sleep();
            network_.mode(resume, boot);
        }
        for (;;) {
            Watchdog::feed();
            uint8_t input[128];
            int count = uart_read_bytes(Uart, input, 1, 1);
            if (count == 1) {
                const int available = uart_read_bytes(Uart, input + 1, sizeof input - 1, 0);
                if (available > 0) count += available;
            }
            auto now = milliseconds();
            if (initialized) {
                const auto activity = bridge_.activity();
                if (activity != bridgeActivity) {
                    bridgeActivity = activity;
                    sleepTimer.activity(now);
                }
                sleepTimer.poll(now, network_.idle(), network_.apHasClients() || bluetooth_.connected() ||
                                portal_.hasClients() || portal_.updating() || files_.active() || bridge_.session());
                if (!sleepTimer.pending() && reply_.data[0] == uint8_t(Result::Sleeping)) haveReply_ = false;
            }
            bool sleepConfirmed = false;
            for (int i = 0; i < count; ++i) {
                Frame frame;
                if (!wire_.feed(input[i], frame)) continue;
                if (frame.command == Command::FetchReply) {
                    if (frame.size || !haveReply_ || frame.sequence != reply_.sequence) continue;
                    uint8_t encoded[Wire::Bytes];
                    const size_t size = Wire::encode(reply_, encoded);
                    uart_write_bytes(Uart, encoded, size);
                    if (sleepTimer.pending() && reply_.data[0] == uint8_t(Result::Sleeping)) {
                        sleepConfirmed = true;
                        break;
                    }
                    continue;
                }
                if (frame.command == Command::Hello && (frame.size != 4 || std::memcmp(frame.data, "OSC1", 4))) continue;
                if (!linked) {
                    if (!initialized && frame.command != Command::Hello) continue;
                    if ((!initialized && !network_.initialize()) ||
                        uart_set_pin(Uart, Transmit, Receive, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK)
                        sleep();
                    linked = initialized = true;
                    sleepTimer.activity(now);
                }
                if (frame.command == Command::EnterSwd) {
                    if (!sleepTimer.pending() && !portal_.updating() && !frame.size && haveReply_ && reply_.command == Command::PrepareSwd &&
                        frame.sequence == uint8_t(reply_.sequence + 1u) && prepared_ && int32_t(now - prepared_) < 0) {
                        uart_driver_delete(Uart);
                        gpio_reset_pin(Transmit);
                        gpio_reset_pin(Receive);
                        vTaskDelay(25); // The STM32 restores both pins to SWD after sending the commit.
                        probe();
                        startUart();
                        wire_ = {};
                        linked = haveReply_ = false;
                        prepared_ = 0;
                        reconnectAt = now = milliseconds();
                        sleepTimer.activity(now);
                        break;
                    }
                    continue;
                }
                if (!haveReply_ || reply_.sequence != frame.sequence || reply_.command != frame.command) {
                    reply_ = {frame.sequence, frame.command, 1, {uint8_t(Result::Ok)}};
                    const bool user = frame.command == Command::Status ? (frame.size == 1 && (frame.data[0] & UserActivity))
                                                                      : frame.command != Command::Exchange;
                    if (frame.command == Command::Sleep && !frame.size) sleepTimer.request(now);
                    else if (user) sleepTimer.activity(now);
                    if (sleepTimer.pending()) reply_.data[0] = uint8_t(Result::Sleeping);
                    else handle(frame, now);
                    haveReply_ = true;
                }
            }
            if (!initialized) {
                if (uint32_t(now - boot) >= 2500) break;
                continue;
            }
            if (!linked && now - reconnectAt >= ReconnectGraceMs && !portal_.updating()) break;
            // Give the instrument time to fetch the sleep status. A continuous capture can defer
            // replies indefinitely, so the ESP still sleeps when the grace period expires.
            if (sleepConfirmed || sleepTimer.due(now)) break;
            PortalRequest change;
            if (network_.receive(change)) {
                apply(change, now);
                sleepTimer.activity(now);
            }
            network_.poll(now);
            portal_.poll(network_.ipv4() != 0);
            bluetooth_.poll(now);
        }
        uart_wait_tx_done(Uart, pdMS_TO_TICKS(60) + 1);
        portal_.stop(); bluetooth_.shutdown(); network_.shutdown();
        uart_driver_delete(Uart);
        sleep();
    }

private:
    void probe() {
        {
            SwdService service(bridge_, milliseconds());
            const uint32_t timeout = (network_.idle() ? network_.idle() : 5u) * 60000u;
            portal_.stop();
            bluetooth_.disconnect();
            network_.useDebugPort(true);
            bridge_.close();
            for (;;) {
                Watchdog::feed();
                const auto now = milliseconds();
                network_.poll(now);
                bluetooth_.poll(now);
                if (!service.poll(now, timeout)) break;
                vTaskDelay(1);
            }
        }
        bluetooth_.disconnect();
        network_.useDebugPort(false);
        bridge_.close();
    }

    void status(bool extended) {
        reply_.data[1] = uint8_t(network_.mode());
        reply_.data[2] = network_.flags() | Flag::WifiSettings | (files_.active() ? Flag::FileRequest : 0) |
                         (bluetooth_.enabled() ? Flag::Ble : 0);
        reply_.data[3] = network_.idle();
        reply_.data[4] = network_.count();
        put32(reply_.data + 5, network_.ipv4());
        put32(reply_.data + 9, network_.passkey());
        if (network_.mode() == Mode::AccessPoint)
            std::strncpy(reinterpret_cast<char *>(reply_.data + LegacyApPasswordOffset), network_.apPassword(),
                         LegacyApPasswordBytes);
        const char *network = network_.mode() == Mode::Station ? network_.ssid() : network_.name();
        unsigned offset = NetworkNameOffset;
        if (extended) {
            reply_.data[offset++] = network_.wifiPower();
            if (network_.mode() == Mode::AccessPoint) {
                const auto size = std::strlen(network_.apPassword());
                reply_.data[offset++] = size;
                std::memcpy(reply_.data + offset, network_.apPassword(), size);
                offset += size;
            }
        }
        const auto size = std::strlen(network);
        std::memcpy(reply_.data + offset, network, size);
        reply_.size = offset + size;
    }
    void apply(const PortalRequest &request, uint32_t now) {
        switch (request.action) {
        case PortalRequest::Action::Join: network_.join(request.ssid, request.password, now); break;
        case PortalRequest::Action::Mode: network_.mode(Mode(request.mode), now); break;
        case PortalRequest::Action::Services:
            if (request.ble != UINT8_MAX) bluetooth_.enable(request.ble, network_.name(), network_.passkey());
            if (request.tcp != UINT8_MAX) network_.setTcp(request.tcp);
            if (request.idle != UINT8_MAX) network_.setIdle(request.idle);
            break;
        case PortalRequest::Action::Time: Network::setUtc(request.epoch); break;
        case PortalRequest::Action::WifiPower: network_.setWifiPower(request.power); break;
        case PortalRequest::Action::ApPassword: network_.setApPassword(request.password, now); break;
        }
    }
    void handle(const Frame &r, uint32_t now) {
        bool ok = true;
        switch (r.command) {
        case Command::Hello: bridge_.close(); status(false); break;
        case Command::Status: status(r.size == 1 && (r.data[0] & IncludeWifiSettings)); break;
        case Command::Mode:
            if (portal_.updating()) { ok = false; break; }
            if (r.size == 1 && r.data[0] == uint8_t(Mode::Station) && !*network_.ssid()) {
                reply_.data[0] = uint8_t(Result::NoSavedNetwork);
                return;
            }
            ok = r.size == 1 && network_.mode(Mode(r.data[0]), now);
            break;
        case Command::Scan: ok = r.size == 0 && network_.scan(); break;
        case Command::Network: {
            const auto *ap = r.size == 1 ? network_.network(r.data[0]) : nullptr;
            if (!ap) { ok = false; break; }
            const auto bytes = strnlen(reinterpret_cast<const char *>(ap->ssid), SsidBytes);
            reply_.data[1] = r.data[0];
            reply_.data[2] = ap->authmode != WIFI_AUTH_OPEN;
            reply_.data[3] = uint8_t(ap->rssi);
            std::memcpy(reply_.data + 4, ap->ssid, bytes);
            reply_.size = 4 + bytes;
            break;
        }
        case Command::Join: {
            const unsigned bytes = r.size ? r.data[0] : 0;
            if (!bytes || bytes > SsidBytes || bytes + 1 > r.size || r.size - bytes - 1 > PasswordBytes ||
                std::memchr(r.data + 1, 0, r.size - 1)) { ok = false; break; }
            char ssid[SsidBytes + 1]{}, password[PasswordBytes + 1]{};
            std::memcpy(ssid, r.data + 1, bytes);
            std::memcpy(password, r.data + 1 + bytes, r.size - bytes - 1);
            ok = !portal_.updating() && network_.join(ssid, password, now);
            break;
        }
        case Command::Bluetooth: ok = r.size == 1 && r.data[0] < 2 && bluetooth_.enable(r.data[0], network_.name(), network_.passkey()); break;
        case Command::Tcp: ok = r.size == 1 && r.data[0] < 2 && network_.setTcp(r.data[0]); break;
        case Command::Idle: ok = r.size == 1 && network_.setIdle(r.data[0]); break;
        case Command::WifiPower: ok = r.size == 1 && network_.setWifiPower(r.data[0]); break;
        case Command::Clock:
            put32(reply_.data + 1, Network::utc()); reply_.size = 5; break;
        case Command::FileExchange: files_.exchange(r, reply_); break;
        case Command::Exchange: bridge_.exchange(r, reply_); break;
        case Command::PrepareSwd:
            ok = !portal_.updating() && r.size == 0 && (network_.ipv4() || bluetooth_.enabled());
            if (ok) prepared_ = now + 5000;
            break;
        default: ok = false; break;
        }
        if (!ok) reply_.data[0] = uint8_t(Result::Failed);
    }
    Bridge bridge_;
    Network network_{bridge_};
    FileMailbox files_;
    Portal portal_{network_, files_};
    Bluetooth bluetooth_{bridge_};
    Wire wire_;
    Frame reply_{};
    bool haveReply_ = false;
    uint32_t prepared_ = 0;
};
}
}

extern "C" void app_main() {
    using namespace openosc::companion;
    Watchdog watchdog;
    if (!watchdog.registered() || nvs_flash_init() != ESP_OK || !ota::validApplication()) {
        ota::selectRecovery();
        esp_restart();
    }
    const auto wake = esp_sleep_get_wakeup_cause();
    const bool standalone = wake == ESP_SLEEP_WAKEUP_UNDEFINED && recoveryRequested();
    const auto resume = ota::loadWebNetwork();
    if (resume.mode != Mode::Off) ota::clearWebNetwork();
    if (!standalone && resume.mode == Mode::Off && wake != ESP_SLEEP_WAKEUP_GPIO) sleep();
    static Controller controller;
    if (standalone) controller.standalone();
    startUart();
    controller.run(resume.mode);
}
