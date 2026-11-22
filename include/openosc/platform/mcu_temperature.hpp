#pragma once
#include <cstdint>
#include <optional>

namespace openosc {
class McuTemperature {
public:
    // One bounded acquisition before the rest of the application starts.
    std::optional<int16_t> captureStartup();
    // Returns the last completed die temperature in tenths of a degree Celsius.
    std::optional<int16_t> poll(uint32_t now, bool enabled);

private:
    bool stop();

    enum class State : uint8_t { Off, Starting, Calibrating, Enabling, Measuring, Stopping, Idle };
    uint32_t startedAt_ = 0;
    std::optional<int16_t> reading_;
    State state_ = State::Off;
};
} // namespace openosc
