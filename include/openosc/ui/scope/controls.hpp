#pragma once
#include "openosc/ui/scope/options.hpp"
#include "openosc/ui/scope/channel_view.hpp"
#include "openosc/ui/editors/property_list.hpp"
#include <cstddef>

namespace openosc::plot {
class Controls {
public:
    enum Row : unsigned {
        Channel,
        View,
        TraceStyle,
        GridStyle,
        Persistence,
        Average,
        PeakHold,
        Glow,
        Zoom,
        Pan,
        CenterTrigger,
        Visible,
        Invert,
        Gain,
        Position,
        TimeCursors,
        VoltageCursors,
        CursorAX,
        CursorBX,
        CursorAY,
        CursorBY,
        MathTrace,
        MeterTrace,
        MeterFunction,
        Reference,
        Clear,
        SampleReduction,
        TraceInterpolation,
        FftSource,
        FftWindow,
        FftScale,
        FftDc,
        CounterSource,
        CounterAverage,
        CounterPpm,
        GridBrightness,
        CounterEstimate, // No edges counted: No signal, Trace f (~) (GitHub #172).
        CounterTimed,    // CH1 below 5 kHz: Gate, Timed edges (GitHub #173).
        Section,
        Count
    };
    enum class Group { View, Capture, Cursors, History, Fft, Counter, Count };
    Group group = Group::View;
    bool listing = true; // The section list (DisplayProperties); false: the group's own rows.
    // The rows of the group (Section past the last).
    unsigned count() const;
    unsigned row(unsigned index) const;
    unsigned index(unsigned row) const;
    void open(unsigned row); // The group holding `row`.
    const char *section() const;
    static const char *groupName(unsigned group);
    Options options;
    int32_t savedPanPpb = 0; // An edit's exact pan (DisplayProperties: the list is rebuilt per key).
    unsigned channel = 0;
    bool enabled[2] = {true, true};
    ChannelView view; // Swap CH1/CH2: rows and choices name each input by its on-screen number.
    // The DMM's function (Preferences::meterFunction, a MeterFunctions index), the MeterFunction row's
    // draft: the DMM trace's and the DMM screen's alike.
    unsigned meterFunction = 0;
    // One row (Row) as a property: what it is, its name and value, its options, its steps.
    ui::PropertyKind kind(unsigned row) const;
    void name(unsigned row, char *output, size_t capacity) const;
    void value(unsigned row, char *output, size_t capacity) const;
    unsigned choices(unsigned row) const;
    unsigned choice(unsigned row) const;
    void choiceName(unsigned row, unsigned index, char *output, size_t capacity) const;
    void adjust(unsigned row, int direction, bool fine = true);
    int32_t number(unsigned row) const;
    void setNumber(unsigned row, int32_t value);
    // The Cursors section's Channel row also offers the DMM trace while it is on (CursorOptions::Meter):
    // the level cursors' channel; `channel`, the CH rows', keeps the last channel chosen.
    bool meterChannel() const { return group == Group::Cursors && options.meter.shown; }
private:
    static constexpr int32_t PpbPerPermille = Options::PanFull / 1000;
    void adjustPan(int direction, bool fine);
    int8_t panEditSign_ = 0;
    uint8_t panEditRepeats_ = 0;
};
} // namespace openosc::plot
