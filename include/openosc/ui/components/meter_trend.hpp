#pragma once
#include "openosc/ui/components/elements.hpp"
#include "openosc/measurement/dmm/session.hpp"
#include "openosc/ui/components/meter_style.hpp"

namespace openosc::ui {
class MeterTrend {
public:
    void create(lv_obj_t *parent, const Theme &theme);
    void visible(bool value) { lv_obj_set_hidden(root_, !value); }
    void palette(const MeterLook &look); // Chart in Background, line in Bar, grid in the base line colour.
    void update(const dmm::History *history, Quantity quantity, double reference, bool relative,
                bool peerRelative = false, const char *temperatureUnit = "degC");
    static constexpr int32_t MarkerSize = 3; // The single sample's marker (px).
private:
    lv_obj_t *root_ = nullptr, *chart_ = nullptr, *range_ = nullptr, *reference_ = nullptr;
    lv_chart_series_t *series_ = nullptr;
    int32_t values_[dmm::History::Capacity]{};
};
} // namespace openosc::ui
