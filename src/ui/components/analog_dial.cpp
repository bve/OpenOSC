#include "openosc/ui/components/analog_dial.hpp"
#include "openosc/ui/components/elements.hpp"
#include "openosc/ui/format/meter.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace openosc::ui {
namespace {
constexpr float Degree = 3.14159265f / 180;
}
lv_obj_t *AnalogDial::create(lv_obj_t *parent) {
    object_ = lv_obj_create(parent);
    lv_obj_remove_style_all(object_);
    lv_obj_set_scrollable(object_, false);
    lv_obj_add_event_cb(object_, draw, LV_EVENT_DRAW_MAIN, this);
    lv_obj_add_event_cb(object_, [](lv_event_t *event) {
        static_cast<AnalogDial *>(lv_event_get_user_data(event))->object_ = nullptr;
    }, LV_EVENT_DELETE, this);
    lv_obj_set_hidden(object_, true);
    decimals_ = 256;
    marking_[0] = 0;
    scale(255, "");
    motion_.reset();
    drawn_ = 0;
    return object_;
}
void AnalogDial::place(int x, int y, int width, int height) {
    lv_obj_set_pos(object_, x, y);
    lv_obj_set_size(object_, width, height);
}
void AnalogDial::visible(bool value) {
    if (!object_ || value == shown()) return;
    lv_obj_set_hidden(object_, !value);
    // Shown again, the needle rises from rest as on switching a meter on; hidden, it stops.
    motion_.reset();
    drawn_ = 0;
}
void AnalogDial::palette(const MeterLook &look) {
    if (look == look_) return;
    look_ = look;
    if (object_) lv_obj_invalidate(object_);
}
void AnalogDial::reading(const dmm::BarReading &value, bool held) {
    const auto state = value.status == DataStatus::Ok ? State::Live
                       : value.status == DataStatus::Overload ? State::Overload
                       : value.status == DataStatus::Stale ? State::Stale : State::NoData;
    // Stale readings carry no counts: the frozen needle keeps its polarity.
    const bool negative = state == State::Stale ? negative_ : state != State::NoData && value.counts < 0;
    if (state == State::Live || state == State::Overload)
        motion_.target(state == State::Overload ? NeedleMotion::High : value.fraction());
    else if (state == State::NoData) motion_.target(0); // Stale keeps the last position.
    const char *caption = MeterText::caption(value.status);
    const bool alert = MeterText::alert(value.status);
    if (state != state_ || negative != negative_ || held != held_ || caption != caption_ || alert != alert_) {
        state_ = state; negative_ = negative; held_ = held; caption_ = caption; alert_ = alert;
        if (shown()) lv_obj_invalidate(object_); // Colours and captions change.
    }
}
void AnalogDial::scale(unsigned decimals, const char *unit, dmm::CountScale counts) {
    char legend[sizeof legend_];
    const bool known = decimals <= 6;
    std::snprintf(legend, sizeof legend, "%s", known ? unit : "x1000 counts");
    if (decimals == decimals_ && counts == counts_ && !std::strcmp(legend, legend_)) return;
    decimals_ = decimals;
    counts_ = counts;
    std::memcpy(legend_, legend, sizeof legend_);
    // Tick i is i * Major thousand counts: counts / 10^decimals in the display's unit. Integer
    // arithmetic only; the firmware's printf has no floating point.
    unsigned long divisor = 1;
    for (unsigned d = 0; known && d < decimals; ++d) divisor *= 10;
    const int major = counts_.major;
    for (int i = 0; i < counts_.labels(); ++i) {
        const unsigned long counts = (unsigned long)(i * major) * (known ? 1000 : 1);
        unsigned long whole = counts / divisor, fraction = counts % divisor;
        int digits = known ? int(decimals) : 0;
        while (digits && fraction % 10 == 0) { fraction /= 10; --digits; } // 0.50 -> 0.5, 1.000 -> 1
        const char *suffix = "";
        if (!digits && whole >= 10000 && whole % 1000 == 0) { whole /= 1000; suffix = "k"; }
        if (digits) std::snprintf(labels_[i], sizeof labels_[i], "%lu.%0*lu%s", whole, digits, fraction, suffix);
        else std::snprintf(labels_[i], sizeof labels_[i], "%lu%s", whole, suffix);
    }
    if (shown()) lv_obj_invalidate(object_);
}
void AnalogDial::marking(const char *text) {
    if (!std::strcmp(text, marking_)) return;
    std::snprintf(marking_, sizeof marking_, "%s", text);
    if (shown()) lv_obj_invalidate(object_);
}
lv_area_t AnalogDial::markingArea() const {
    lv_area_t area;
    lv_obj_get_coords(object_, &area);
    lv_point_t size;
    lv_text_get_size(&size, marking_, look_.text(MeterRole::Details, MarkingFont), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return {area.x2 - size.x + 1, area.y1 + MarkingY, area.x2, area.y1 + MarkingY + MarkingHeight - 1};
}
lv_area_t AnalogDial::numberArea(int major) const {
    const auto g = geometry();
    const float a = angle(float(major * counts_.major) / counts_.ticks()) * Degree;
    const float r = g.radius + LabelOffset;
    const int x = int(std::lround(g.px + std::sin(a) * r)), y = int(std::lround(g.py - std::cos(a) * r));
    return {x - 20, y - 7, x + 20, y + 7};
}
AnalogDial::Geometry AnalogDial::geometry() const {
    lv_area_t area;
    lv_obj_get_coords(object_, &area);
    const float half = float(lv_area_get_width(&area) - 1) / 2;
    const float radius = std::max(10.f, (half - SideMargin) / std::sin(HalfAngle * Degree));
    return {float(area.x1) + half, float(area.y1) + TopMargin + radius, radius};
}
lv_area_t AnalogDial::needleArea(float fraction) const {
    const auto g = geometry();
    const float a = angle(fraction) * Degree, s = std::sin(a), c = std::cos(a);
    lv_area_t object;
    lv_obj_get_coords(object_, &object);
    // From the face's bottom edge (where the needle enters) to the tip.
    const float tipX = g.px + s * (g.radius + 2), tipY = g.py - c * (g.radius + 2);
    const float baseY = float(object.y2), baseX = g.px + s * (g.py - baseY) / std::max(c, .05f);
    return {int32_t(std::floor(std::min(tipX, baseX))) - 3, int32_t(std::floor(tipY)) - 3,
            int32_t(std::ceil(std::max(tipX, baseX))) + 3, object.y2};
}
bool AnalogDial::service(uint32_t now) {
    if (!shown()) return false;
    const bool moved = motion_.step(now);
    const float position = motion_.position();
    if (position == drawn_ || (!moved && !motion_.settled())) return false;
    if (!motion_.settled() && uint32_t(now - drawnAt_) < FrameMs) return false;
    const lv_area_t old = needleArea(drawn_), next = needleArea(position);
    lv_area_t both = {std::min(old.x1, next.x1), std::min(old.y1, next.y1), std::max(old.x2, next.x2), std::max(old.y2, next.y2)};
    lv_obj_invalidate_area(object_, &both);
    drawn_ = position;
    drawnAt_ = now;
    return true;
}
void AnalogDial::draw(lv_event_t *event) {
    auto &self = *static_cast<AnalogDial *>(lv_event_get_user_data(event));
    self.paint(lv_event_get_layer(event));
}
void AnalogDial::paint(lv_layer_t *layer) const {
    lv_area_t area;
    lv_obj_get_coords(object_, &area);
    const auto g = geometry();
    const bool live = state_ == State::Live || state_ == State::Overload;
    // Live: each part in its own colour; no live reading: all of the scale muted, as before.
    const auto part = [&](MeterRole role) { return lv_color_hex(live ? look_.color(role) : look_.palette.muted); };
    const auto scale = part(MeterRole::ScaleNumbers);
    lv_draw_rect_dsc_t face;
    lv_draw_rect_dsc_init(&face);
    face.bg_color = lv_color_hex(look_.color(MeterRole::Background));
    lv_draw_rect(layer, &face, &area);
    // The arc, its overload zone past full scale, the ticks and the numbers.
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center = {int32_t(std::lround(g.px)), int32_t(std::lround(g.py))};
    arc.radius = uint16_t(g.radius);
    arc.width = look_.arcWidth; arc.color = part(MeterRole::Arc);
    arc.start_angle = 270 - HalfAngle; arc.end_angle = 270 + HalfAngle;
    lv_draw_arc(layer, &arc);
    arc.width = 4; arc.color = lv_color_hex(look_.palette.alert);
    arc.start_angle = 270 + HalfAngle; arc.end_angle = 270 + angle(NeedleMotion::High);
    lv_draw_arc(layer, &arc);
    lv_draw_line_dsc_t tick;
    lv_draw_line_dsc_init(&tick);
    tick.color = part(MeterRole::Ticks);
    lv_draw_label_dsc_t number;
    lv_draw_label_dsc_init(&number);
    number.font = look_.text(MeterRole::ScaleNumbers, 12); number.color = scale; number.align = LV_TEXT_ALIGN_CENTER;
    // A needle frame redraws a thin strip: what lies outside it is not even queued.
    const lv_area_t &clip = layer->_clip_area;
    const auto hits = [&](float x1, float y1, float x2, float y2, float pad) {
        return std::min(x1, x2) - pad <= clip.x2 && std::max(x1, x2) + pad >= clip.x1 &&
               std::min(y1, y2) - pad <= clip.y2 && std::max(y1, y2) + pad >= clip.y1;
    };
    const int ticks = counts_.ticks(), step = counts_.major;
    for (int i = 0; i <= ticks; ++i) {
        const bool major = i % step == 0;
        const float a = angle(float(i) / ticks) * Degree, s = std::sin(a), c = std::cos(a);
        const float inner = g.radius - (major ? 9 : 5);
        tick.width = look_.tickWidth + (major ? 1 : 0);
        tick.p1 = {lv_value_precise_t(g.px + s * g.radius), lv_value_precise_t(g.py - c * g.radius)};
        tick.p2 = {lv_value_precise_t(g.px + s * inner), lv_value_precise_t(g.py - c * inner)};
        if (hits(tick.p1.x, tick.p1.y, tick.p2.x, tick.p2.y, tick.width + 1)) lv_draw_line(layer, &tick);
        if (!major) continue;
        const lv_area_t box = numberArea(i / step);
        if (!hits(box.x1, box.y1, box.x2, box.y2, 0)) continue;
        number.text = labels_[i / step]; number.text_local = 1;
        lv_draw_label(layer, &number, &box);
    }
    // The unit of the numbers, under the top of the arc.
    lv_draw_label_dsc_t unit;
    lv_draw_label_dsc_init(&unit);
    unit.font = look_.text(MeterRole::ScaleNumbers, 14); unit.color = scale; unit.align = LV_TEXT_ALIGN_CENTER;
    unit.text = legend_; unit.text_local = 1;
    const int centre = int(std::lround(g.px)), below = int(std::lround(g.py - g.radius)) + 14;
    const lv_area_t legendBox = {centre - 60, below, centre + 60, below + 16};
    lv_draw_label(layer, &unit, &legendBox);
    // The marking (jacks, RMS) in the top right corner.
    if (*marking_) {
        lv_draw_label_dsc_t mark;
        lv_draw_label_dsc_init(&mark);
        mark.font = look_.text(MeterRole::Details, MarkingFont); mark.color = lv_color_hex(look_.color(MeterRole::Details));
        mark.align = LV_TEXT_ALIGN_RIGHT; mark.text = marking_; mark.text_local = 1;
        const lv_area_t box = {area.x1 + lv_area_get_width(&area) / 2, area.y1 + MarkingY, area.x2, area.y1 + MarkingY + MarkingHeight - 1};
        lv_draw_label(layer, &mark, &box);
    }
    // Bottom left, in reading order: the polarity, then the state (OL, HOLD...) after it.
    const char *caption = this->caption();
    lv_draw_label_dsc_t text;
    lv_draw_label_dsc_init(&text);
    const int signLeft = area.x1 + SignX;
    if (negative_) {
        text.font = Elements::font(24); text.text = "\xe2\x88\x92"; text.color = lv_color_hex(look_.color(MeterRole::Needle)); // − U+2212
        const lv_area_t sign = {signLeft, area.y2 - 26, signLeft + SignWidth - 1, area.y2};
        lv_draw_label(layer, &text, &sign);
    }
    text.font = Elements::font(14);
    text.color = lv_color_hex(captionAlert() ? look_.palette.alert : look_.palette.muted);
    text.text = caption; text.text_local = 1;
    const int captionLeft = negative_ ? signLeft + SignWidth + 2 : signLeft;
    const lv_area_t left = {captionLeft, area.y2 - 17, captionLeft + 80, area.y2};
    if (*caption) lv_draw_label(layer, &text, &left);
    // The needle, from the bottom edge to just past the arc.
    const float a = angle(drawn_) * Degree, s = std::sin(a), c = std::cos(a);
    lv_draw_line_dsc_t needle;
    lv_draw_line_dsc_init(&needle);
    needle.color = lv_color_hex(state_ == State::Overload ? look_.palette.alert : look_.color(MeterRole::Needle));
    needle.width = look_.needleWidth; needle.round_end = 1;
    const float along = (g.py - float(area.y2 + 1)) / std::max(c, .05f); // Pivot to the bottom edge.
    needle.p1 = {lv_value_precise_t(g.px + s * along), lv_value_precise_t(area.y2 + 1)};
    needle.p2 = {lv_value_precise_t(g.px + s * (g.radius + 2)), lv_value_precise_t(g.py - c * (g.radius + 2))};
    lv_draw_line(layer, &needle);
}
} // namespace openosc::ui
