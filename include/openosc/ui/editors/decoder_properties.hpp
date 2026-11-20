#pragma once
#include "openosc/decode/settings.hpp"
#include "openosc/ui/editors/property_list.hpp"

namespace openosc::ui {
// Analyze > Decoder (and F1 Protocol / F2 View on the decoder's screen): decoding::Settings, each
// committed field taken at once by the controller (saved, the job reconfigured; no Apply). The rows
// follow the protocol: the common ones (Protocol, View, Update), the protocol's own (lines, rates,
// formats, roles, LIN lengths, CAN source and the nominal and data rates and sample points of Classical
// CAN and ISO CAN FD), then each line's threshold named by its role (RX, SDA,
// SCL, SCLK, data, RXD, CANH-CANL), the hysteresis (Auto: 10 % of each line's swing in the record; Manual:
// its width in mV, a row of its own), the minimum pulse and Save CSV (the result it saves
// named as its value; an action the controller runs). Protocol Off leaves the
// decoder; Live (no accepted snapshot) is listed and refused with its reason.
// Numbers: rates step through the standard ones with Up/Down and by 1 % with Left/Right (the UART
// baud: to the neighbour of three significant digits or a standard rate, reversible on that scale,
// an off-scale rate snaps to it); thresholds
// by 100 mV / 1 mV; the SPI pause through 1-2-5 / 1 %. The CAN data rate is never below the nominal:
// raising the nominal raises it with it.
class DecoderProperties final : public PropertyList {
public:
    enum class Row : uint8_t {
        Protocol, View, Update,
        Line, UartBaud, DetectBaud, DataBits, Parity, StopBits, BitOrder, IdleLevel, // UART (Line: LIN too)
        Sda,                                                                     // I2C
        Sclk, SpiMode, WordBits, SpiOrder, WordStart, PauseNs, ManualStart,      // SPI
        LinBaud, LinChecksum, LinId, LinLength,                                  // LIN
        CanSource, CanBitrate, SamplePoint, DataBitrate, DataSamplePoint,        // CAN (Classical and ISO CAN FD)
        Threshold1, Threshold2, Difference, HysteresisMode, Hysteresis, MinPulse, // Hysteresis: Manual only
        SaveCsv,                                                                 // Stage 7: the shown result to a file
    };
    explicit DecoderProperties(const decoding::Settings &settings = {});
    const decoding::Settings &settings() const { return settings_; }
    // Save CSV's value: the result it would save ("UART 15 ev #123", "no result"): what is saved is said
    // before it is chosen. Copied.
    void result(const char *summary);
    // Detect baud's value: the search's state ("HOLD, MENU", "measuring 40%", "2500000? MENU", "no sure
    // rate"); the controller runs it and applies a confirmed rate only on the next MENU. Copied.
    void baudHint(const char *state);
    const char *baudHint() const { return hint_; }
    unsigned index(Row row) const; // The row's index in this protocol's list (count() when it has none).
    Row row(unsigned index) const { return rows_[index < count_ ? index : 0]; }
    const char *title() const override { return "DECODER"; }
    const char *notice() const override;
    unsigned count() const override { return count_; }
    PropertyKind kind(unsigned row) const override;
    void name(unsigned row, char *output, size_t capacity) const override;
    void value(unsigned row, char *output, size_t capacity) const override;
    unsigned options(unsigned row) const override;
    unsigned option(unsigned row) const override;
    void optionName(unsigned row, unsigned index, char *output, size_t capacity) const override;
    bool choose(unsigned row, unsigned index) override;
    void adjust(unsigned row, int steps) override;
    int coarseStep(unsigned row) const override;
    int32_t number(unsigned row) const override;
    void setNumber(unsigned row, int32_t value) override;
private:
    static constexpr unsigned MaximumRows = 20;
    void layout(); // rows_ for the protocol in settings_.
    decoding::Settings settings_;
    Row rows_[MaximumRows]{};
    uint8_t count_ = 0;
    uint8_t linId_ = 0;              // The ID whose length the Length row edits (not saved).
    const char *refused_ = nullptr;  // The last refused choice's reason (notice()), until the next choice.
    char result_[24] = "no result", hint_[24] = "HOLD, MENU";
};
} // namespace openosc::ui
