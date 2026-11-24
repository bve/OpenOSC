#pragma once
#include "openosc/ui/components/elements.hpp"
#include "openosc/measurement/dmm/session.hpp"
#include "openosc/measurement/dmm/resolution.hpp"
#include "openosc/ui/components/meter_bar.hpp"

namespace openosc::ui {
// Range/REL reference, secondary reading and range mode on one row; below it MIN, MAX and AVG in
// three columns: their names in one row, the values under them on one baseline, in one font (the
// largest of 14 and 12 in which all three fit their column with the sign column reserved). The three
// values use the shown reading's unit and resolution (dmm::Resolution: AVG never has more decimals
// than the readings); one from another range that does not fit there keeps the display's five digits.
class MeterSummary {
public:
    static constexpr int Width = 296, Columns = 3, ColumnGap = 4, ColumnWidth = (Width - (Columns - 1) * ColumnGap) / Columns;
    static constexpr int NameY = 28, ValueY = 41, ValueHeight = 16;
    // Digital meta row: the reference, the secondary reading (at most SecondaryWidth, MetaGap clear
    // of the range mode), the range mode at its text's width against the right edge.
    static constexpr int MetaHeight = 16, ReferenceWidth = 150, SecondaryX = 154, SecondaryWidth = 80, MetaGap = 4;
    static constexpr unsigned ValueFont = 14;
    static constexpr int columnX(unsigned column) { return int(column) * (ColumnWidth + ColumnGap); }
    void create(lv_obj_t *parent);
    void visible(bool value);
    // Names, the three values, the range/secondary texts, the separator and the bar's base (MeterLook).
    void palette(const MeterLook &look);
    // The analog dial shows the magnitude: no bar.
    void bar(bool shown) { bar_.visible(shown); }
    // The analog layout: the reference, range mode and secondary reading on one 14 px meta row at
    // `metaY` (right of the function sign, CompactMetaX on), MIN/MAX/AVG on one 14 px row at
    // `statsY` (-1: no such row, it is hidden), each name + value group centred in its third; no
    // separator; both rows independent, as the dial and the statistics may stand anywhere in the
    // order. Off: the digital arrangement above. The meta row flows by the texts' widths: the
    // reference, the range mode CompactMetaGap after it, the secondary reading (AC frequency)
    // right-aligned in the rest.
    void compact(bool on, int metaY = 0, int statsY = 0);
    // Digital: whether MIN/MAX/AVG (or the trend) have their rows. Without them the bar moves up
    // under the meta row (ScreenLayout::DigitalMeterNoStatistics rows in all), leaving no hole.
    void statisticsRows(bool rows);
    static constexpr int CompactMetaX = 60, CompactRow = 14, CompactNameGap = 4, CompactMetaGap = 6;
    static constexpr unsigned CompactFont = 12;
    static constexpr int BarTop = 63, Height = 89; // The bar's rows: BarTop..Height.
    // `charge`: the ampere-hour counter's text for the secondary slot when the reading has no secondary
    // value of its own (DC current; null or empty: none).
    void update(const dmm::Result &result, const dmm::Statistics *statistics, uint32_t accent,
                double reference = 0, bool relative = false, bool peerRelative = false, const char *charge = nullptr);
    void bar(const dmm::Result &result, DataStatus status, uint32_t accent,
             const dmm::RelativeReading *relative = nullptr, dmm::TemperatureAxis *axis = nullptr) {
        bar_.update(result, status, accent, relative, axis);
    }
    // MIN, MAX, AVG (tests).
    lv_obj_t *statistic(unsigned column) const { return values_[column]; }
    lv_obj_t *statisticName(unsigned column) const { return names_[column]; }
    lv_obj_t *rangeMode() const { return range_; } // AUTO / MANUAL (tests).
    lv_obj_t *secondary() const { return secondary_; } // The secondary reading, the AC frequency (tests).
    lv_obj_t *reference() const { return reference_; } // Range or REL reference (tests).

private:
    void arrange();
    void centre(unsigned column);
    void flowMeta(); // The meta row's three texts at their widths (digital and compact).
    // A statistic at the last valid reading's resolution, else at the display's digits.
    void statisticText(double value, Quantity quantity, char *output, size_t capacity, bool relative = false) const;
    static int textWidth(lv_obj_t *label); // Its text in its own font (the role's typeface).
    MeterLook look_{};
    bool compact_ = false, statisticsRows_ = true;
    int metaY_ = 0, statsY_ = 0;
    lv_obj_t *root_ = nullptr, *range_ = nullptr, *secondary_ = nullptr, *separator_ = nullptr;
    lv_obj_t *names_[Columns] = {}, *values_[Columns] = {};
    MeterBar bar_;
    lv_obj_t *reference_ = nullptr;
    dmm::Resolution resolution_; // Of the last valid reading: an overload keeps the period's format.
};
} // namespace openosc::ui
