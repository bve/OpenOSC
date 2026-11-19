#pragma once
#include "openosc/ui/scope/options.hpp"
#include "openosc/ui/editors/property_list.hpp"
#include <cstddef>

namespace openosc::plot {
class Model;
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
    const Model *model = nullptr; // The view the Pan row steps through: a pixel (fine) or a division (TraceMove::panStep).
    unsigned channel = 0;
    bool enabled[2] = {true, true};
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
};
} // namespace openosc::plot
