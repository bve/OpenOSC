#include "openosc/ui/components/generator_preview.hpp"
#include "openosc/ui/components/elements.hpp"
#include "openosc/generator/capabilities.hpp"
#include <cstring>

namespace openosc::ui {
lv_obj_t *GeneratorPreview::create(lv_obj_t *parent) {
    valid_ = false;
    object_ = lv_obj_create(parent);
    lv_obj_remove_style_all(object_);
    lv_obj_set_scrollable(object_, false);
    lv_obj_add_event_cb(object_, draw, LV_EVENT_DRAW_MAIN, this);
    return object_;
}
void GeneratorPreview::update(const generator::Settings &settings, uint32_t color, const uint16_t *samples, unsigned points, uint32_t revision) {
    char level[sizeof level_];
    generator::LevelText::describe(settings, level, sizeof level);
    const uint32_t shown = settings.arbitrary() && samples ? revision + 1 : 0; // Which waveform it draws (0: a built one).
    if (valid_ && shape_ == settings.shape && duty_ == settings.dutyPermille && color_ == color && revision_ == shown && !std::strcmp(level, level_))
        return;
    std::memcpy(level_, level, sizeof level_);
    revision_ = shown;
    if (settings.arbitrary()) { // The loaded codes, one in points/96 (their full 0..4095 span on the 0..1000 grid).
        valid_ = samples && generator::ArbitraryLimits::points(points);
        for (unsigned i = 0; valid_ && i < 96; ++i)
            samples_[i] = uint16_t(uint32_t(samples[i * points / 96]) * 1000 / generator::ArbitraryLimits::MaximumCode);
    } else {
        auto preview = settings;
        preview.level=generator::Level::Custom;
        preview.minimumCode = 0;
        preview.maximumCode = 1000;
        valid_ = generator::WaveTable::build(preview, samples_, 96);
    }
    shape_ = settings.shape;
    duty_ = settings.dutyPermille;
    color_ = color;
    lv_obj_invalidate(object_);
}
void GeneratorPreview::draw(lv_event_t *event) {
    auto &self = *static_cast<GeneratorPreview *>(lv_event_get_user_data(event));
    if (!self.valid_) return;
    lv_area_t area;
    lv_obj_get_coords(self.object_, &area);
    const int width = lv_area_get_width(&area), height = lv_area_get_height(&area);
    if (height < 24 || width < 2) return;
    lv_draw_label_dsc_t label;
    lv_draw_label_dsc_init(&label);
    label.text = "PREVIEW";
    label.font = Elements::font(12);
    label.color = lv_color_hex(self.color_);
    lv_area_t caption = {area.x1, area.y1, area.x2, area.y1 + 13};
    if (height>=50) lv_draw_label(lv_event_get_layer(event), &label, &caption);
    const bool footer = height >= 60;
    if (footer) {
        label.text = self.level_; // What sets the level (no calibrated amplitude exists).
        label.color = lv_color_hex(0x94a3b8);
        lv_area_t areaFooter = {area.x1, area.y2 - 14, area.x2, area.y2};
        lv_draw_label(lv_event_get_layer(event), &label, &areaFooter);
    }
    lv_point_precise_t points[96];
    for (unsigned i = 0; i < 96; ++i)
        points[i] = {lv_value_precise_t(area.x1 + i * (width - 1) / 95),
                     lv_value_precise_t(area.y1 + (height < 50 ? 2 : 16) + (1000 - self.samples_[i]) * (height < 50 ? height-4 : height - (footer ? 37 : 18)) / 1000)};
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(self.color_);
    line.width = 1;
    line.points = points;
    line.point_cnt = 96;
    lv_draw_line(lv_event_get_layer(event), &line);
}
} // namespace openosc::ui
