#pragma once
#include "openosc/board/current.hpp"
#include "openosc/ui/editors/property_list.hpp"
#include "openosc/measurement/dmm/hold.hpp"
#include "openosc/measurement/dmm/result.hpp"
#include "openosc/measurement/dmm/temperature.hpp"
#include "openosc/ui/format/meter.hpp"
#include "openosc/settings/theme.hpp"
#include <optional>

namespace openosc::ui {
class MeterProperties final : public PropertyList {
public:
    // Hold: its own list (MeterHoldProperties), the mode as its value.
    enum Row { Range, Display, Colors, Digits, Appearance, Layout, Statistics, BarGraph, BarUnit, Relative, Hold, ResetStatistics,
               Logging, SessionLog, JunctionSource, ColdJunction, TemperatureUnit, SerialOutput, Count };
    static constexpr unsigned VisibleRows = board::Current::Features.dmmSerialOutput ? Count : SerialOutput;
    explicit MeterProperties(const dmm::Result &result, unsigned display = 0, bool statistics = true,
                             bool relative = false, bool logging = false, bool serial=false, bool jackBusy=false,
                             unsigned face = 0, unsigned palette = 0, const Theme *theme = nullptr,
                             dmm::HoldMode hold = dmm::HoldMode::Manual)
        : result_(result), display_(display), face_(face), palette_(palette), statistics_(statistics), relative_(relative),
          logging_(logging),serial_(serial),jackBusy_(jackBusy), theme_(theme), hold_(hold) {}
    unsigned display() const { return display_; }
    unsigned face() const { return face_; }
    unsigned palette() const { return palette_; }
    bool statistics() const { return statistics_; }
    bool barGraph() const { return bar_; }
    MeterProperties &barGraph(bool shown) { bar_ = shown; return *this; }
    bool barUnit() const { return barUnit_; } // The unit after the bar scale's last number.
    MeterProperties &barUnit(bool shown) { barUnit_ = shown; return *this; }
    bool relative() const { return relative_; }
    bool automatic() const { return result_.range == dmm::RangeMode::Auto; }
    bool logging() const { return logging_; }
    MeterProperties &session(bool recording) { session_ = recording; return *this; } // Session log's value: REC.
    bool serial() const { return serial_; }
    dmm::TemperatureSettings temperature() const { return temperature_; }
    MeterProperties &temperature(dmm::TemperatureSettings settings) { temperature_ = settings; return *this; }
    MeterProperties &mcuJunction(std::optional<int16_t> reading) { mcuJunction_ = reading; return *this; }
    const char *notice() const override { return MeterText::function(result_); }
    const char *title() const override { return "MEASUREMENT"; }
    unsigned count() const override { return VisibleRows; }
    PropertyKind kind(unsigned row) const override;
    void name(unsigned row, char *output, size_t capacity) const override;
    void value(unsigned row, char *output, size_t capacity) const override;
    unsigned options(unsigned row) const override;
    unsigned option(unsigned row) const override;
    void optionName(unsigned row, unsigned index, char *output, size_t capacity) const override;
    bool choose(unsigned row, unsigned index) override;
    void adjust(unsigned row, int step) override;
    int coarseStep(unsigned) const override { return 10; }
    int32_t number(unsigned row) const override;
    void setNumber(unsigned row, int32_t value) override;
    void beginEdit(unsigned row) override;
    // Base colors: each palette's body as its sample (as Appearance > Base colors); needs the theme
    // in force (a stable one: the owner's), without it no sample.
    bool optionSwatch(unsigned row, unsigned index, uint32_t &rgb) const override;
    bool valueSwatch(unsigned row, uint32_t &rgb) const override { return optionSwatch(row, palette_, rgb); }
private:
    bool ranged() const; // The function shown has ranges (dmm::Ranges::selectable): else Range is Info, "None".
    dmm::Result result_;
    unsigned display_, face_, palette_;
    bool statistics_, relative_, logging_,serial_,jackBusy_, bar_ = true, barUnit_ = true, session_ = false;
    const Theme *theme_;
    dmm::HoldMode hold_;
    dmm::TemperatureSettings temperature_;
    std::optional<int16_t> mcuJunction_;
};
} // namespace openosc::ui
