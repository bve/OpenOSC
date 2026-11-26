#pragma once
#include <cstddef>
#include <cstdint>

namespace openosc::ui {
class FrequencyEditor {
public:
    // Millihertz as uint64: the ZT706S module's 10 MHz is 1e10 mHz. `samples`: an Arbitrary waveform's
    // sample rate in mS/s instead (S/s units, whole samples a second: no digit below 1 S/s).
    void begin(uint64_t millihertz, uint64_t maximum, uint64_t minimum = 1, bool samples = false);
    bool samples() const { return samples_; }
    void move(int direction);
    void adjust(int direction);
    uint64_t value() const { return value_; }
    uint64_t step() const;
    // Character of the digit being edited in text(): its decimal point after integerDigits().
    unsigned digit() const { const unsigned index = digits() - 1 - power_; return index + (index >= integerDigits() ? 1 : 0); }
    void nextUnit() { unit_ = (unit_ + 1) % 3; }
    const char *unit() const {
        static constexpr const char *names[] = {"Hz", "kHz", "MHz"}, *rates[] = {"S/s", "kS/s", "MS/s"};
        return samples_ ? rates[unit_] : names[unit_];
    }
    const char *baseUnit() const { return samples_ ? "S/s" : "Hz"; } // Of value() / 1000.
    void text(char *output, size_t capacity) const;

private:
    // 10 digits up to 9 999 999.999 Hz; one more where the maximum reaches 10 MHz (ZT706S).
    unsigned digits() const { return maximum_ >= 10000000000ULL ? 11 : 10; }
    unsigned integerDigits() const { return digits() - 3 - unit_ * 3; }
    uint64_t value_ = 1000000, maximum_ = 1000000000, minimum_ = 1;
    unsigned power_ = 3, unit_ = 0;
    bool samples_ = false;
};
} // namespace openosc::ui
