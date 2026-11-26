#pragma once
#include "lvgl.h"
#include "openosc/generator/waveform.hpp"

namespace openosc::ui {
class GeneratorPreview {
public:
    lv_obj_t *create(lv_obj_t *parent);
    // An Arbitrary setting draws `samples` (its `points` codes, `revision` the loaded one's); none: no curve.
    void update(const generator::Settings &settings, uint32_t color, const uint16_t *samples = nullptr, unsigned points = 0, uint32_t revision = 0);

private:
    static void draw(lv_event_t *event);
    lv_obj_t *object_ = nullptr;
    uint16_t samples_[96]{};
    char level_[48] = ""; // generator::LevelText::describe(): the footer line.
    generator::Shape shape_ = generator::Shape::Sine;
    uint16_t duty_ = 0;
    uint32_t revision_ = 0;
    uint32_t color_ = 0;
    bool valid_ = false;
};
} // namespace openosc::ui
