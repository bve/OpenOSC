#include "openosc/ui/editors/trigger_editor.hpp"
#include "openosc/ui/scope/format.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace openosc::ui {
using scope::TriggerCondition;
using scope::TriggerType;
bool TriggerEditor::midpoint(const scope::FrameView &frame, scope::Channel channel, double &code) {
    const unsigned source = unsigned(channel);
    if (source >= 2) return false;
    scope::SignalBand bands[2];
    const auto &band=bands[source];
    if (!scope::SignalBands::measure(frame,bands) || !band.valid) return false;
    code = (int(band.low) + band.high) * .5;
    const auto &m = frame.metadata().channels[source];
    // The quantized extrema each resolve to a code. Within half a code, a
    // band straddling the calibrated zero is consistent with a centered signal.
    if (m.calibrated && band.low <= m.zeroCode && m.zeroCode <= band.high && std::abs(code - m.zeroCode) <= .5)
        code = m.zeroCode;
    return true;
}
unsigned TriggerEditor::rows(Field (&out)[Fields]) const {
    unsigned n = 0;
    for (const Field f : {Type, Source, Slope, Level}) out[n++] = f;
    if (settings_.qualified()) {
        if (settings_.twoLevels()) out[n++] = Level2;
        out[n++] = Condition;
        const auto c = settings_.condition;
        if (c == TriggerCondition::Greater || windowed()) out[n++] = Lower;
        if (c == TriggerCondition::Less || windowed()) out[n++] = Upper;
    }
    for (const Field f : {Mode, Pretrigger, Center, Reject, Midpoint}) out[n++] = f;
    return n;
}
unsigned TriggerEditor::count() const { Field shown[Fields]; return rows(shown); }
TriggerEditor::Field TriggerEditor::field(unsigned row) const {
    Field shown[Fields];
    return row < rows(shown) ? shown[row] : Fields;
}
unsigned TriggerEditor::row(Field f) const {
    Field shown[Fields];
    const unsigned n = rows(shown);
    for (unsigned r = 0; r < n; ++r) if (shown[r] == f) return r;
    return n;
}
bool TriggerEditor::primary(Field f) const {
    return !settings_.twoLevels() || (f == Level) == (settings_.level >= settings_.level2);
}
int16_t &TriggerEditor::levelField(Field f) { return primary(f) ? settings_.level : settings_.level2; }
int16_t TriggerEditor::levelValue(Field f) const { return primary(f) ? settings_.level : settings_.level2; }
PropertyKind TriggerEditor::kind(unsigned row) const {
    switch (field(row)) {
    case Level: case Level2: case Pretrigger: case Lower: case Upper: return PropertyKind::Number;
    case Center: case Midpoint: return PropertyKind::Action;
    case Reject: return PropertyKind::Toggle;
    default: return PropertyKind::Choice;
    }
}
void TriggerEditor::name(unsigned row, char *output, size_t capacity) const {
    static constexpr const char *names[] = {"Type", "Source", "Edge", "Level", "Mode", "Pretrigger", "Center trigger",
                                            "Noise reject", "Set level to 50%", "Low level", "Time", "Lower limit", "Upper limit"};
    const Field f = field(row);
    const char *text = f < Fields ? names[f] : "";
    if (f == Slope && settings_.qualified()) text = settings_.type == TriggerType::Transition ? "Slope" : "Polarity";
    if (f == Level && settings_.twoLevels()) text = "High level";
    const bool raw = (f == Level || f == Level2) && !calibration_[unsigned(settings_.channel)].calibrated;
    std::snprintf(output, capacity, raw ? "%s (raw)" : "%s", text);
}
unsigned TriggerEditor::options(unsigned row) const {
    switch (field(row)) {
    case Source: case Slope: case Reject: return 2;
    case Mode: return 3;
    case Type: return 4;
    case Condition: return settings_.type == TriggerType::Runt ? 5 : 4; // Any: a runt of any width.
    default: return 0;
    }
}
unsigned TriggerEditor::option(unsigned row) const {
    switch (field(row)) {
    case Source: return unsigned(settings_.channel);
    case Slope: return unsigned(settings_.edge);
    case Mode: return unsigned(settings_.mode);
    case Reject: return unsigned(settings_.reject);
    case Type: return unsigned(settings_.type);
    case Condition: {
        const unsigned c = unsigned(settings_.condition);
        return (c + 4) % 5; // Any is displayed when sent over SCPI, and selectable for Runt.
    }
    default: return 0;
    }
}
void TriggerEditor::optionName(unsigned row, unsigned index, char *output, size_t capacity) const {
    static constexpr const char *modes[] = {"Auto", "Normal", "Single"};
    static constexpr const char *types[] = {"Edge", "Pulse width", "Runt", "Rise/fall time"};
    static constexpr const char *conditions[] = {"Less than", "More than", "Between", "Outside", "Any"};
    const Field f = field(row);
    const bool positive = !index;
    if (f == Source) std::snprintf(output, capacity, "%s", view_.inputLabel(index));
    else if (f == Slope) std::snprintf(output, capacity, "%s", !settings_.qualified() ? (positive ? "Rising" : "Falling")
                                                       : settings_.type == TriggerType::Transition ? (positive ? "Rise" : "Fall")
                                                       : positive ? "Positive" : "Negative");
    else if (f == Mode && index < 3) std::snprintf(output, capacity, "%s", modes[index]);
    else if (f == Type && index < 4) std::snprintf(output, capacity, "%s", types[index]);
    else if (f == Condition && index < 5) std::snprintf(output, capacity, "%s", conditions[index]);
    else PropertyList::optionName(row, index, output, capacity); // Noise reject: Off/On.
}
void TriggerEditor::value(unsigned row, char *output, size_t capacity) const {
    const Field f = field(row);
    if (f == Level || f == Level2) {
        const int level = levelValue(f);
        const auto &cal = calibration_[unsigned(settings_.channel)];
        if (cal.calibrated)
            plot::Format::quantity(scope::TriggerLevel::shownVolts(level, cal),"V",output,capacity);
        else std::snprintf(output,capacity,"%d cnt",level);
        if (const int at = end(level)) { // At either end of the plot's range: no further step that way.
            const size_t used = std::strlen(output);
            if (used < capacity) std::snprintf(output + used, capacity - used, at > 0 ? " (max)" : " (min)");
        }
    } else if (f == Lower || f == Upper) {
        const uint32_t ns = f == Lower ? settings_.minimumNs : settings_.maximumNs;
        if (!plot::Format::quantity(ns * 1e-9, "s", output, capacity, 3)) std::snprintf(output, capacity, "%lu ns", (unsigned long)ns);
    } else if (f == Pretrigger) std::snprintf(output,capacity,"%u.%u%%",settings_.position/10,settings_.position%10);
    else PropertyList::value(row, output, capacity);
}
int32_t TriggerEditor::number(unsigned row) const {
    switch (field(row)) {
    case Level: case Level2: return levelValue(field(row));
    case Pretrigger: return settings_.position;
    case Lower: return int32_t(settings_.minimumNs);
    case Upper: return int32_t(settings_.maximumNs);
    default: return 0;
    }
}
void TriggerEditor::setNumber(unsigned row, int32_t value) {
    switch (field(row)) {
    case Level: case Level2: levelField(field(row)) = int16_t(value); break;
    case Pretrigger: settings_.position = uint16_t(value); break;
    case Lower: settings_.minimumNs = uint32_t(value); break;
    case Upper: settings_.maximumNs = uint32_t(value); break;
    default: break;
    }
}
uint32_t TriggerEditor::stepTime(uint32_t ns, int steps) {
    static constexpr uint32_t Mantissas[TimeSteps] = {10, 12, 15, 20, 25, 30, 40, 50, 60, 70, 80, 90};
    // The steps as one ascending list: 1..9 ns, then the mantissas x 10^d ns from 10 ns to 1 s.
    const auto valueAt = [](int index) {
        if (index < 9) return uint64_t(index + 1);
        index -= 9;
        uint64_t v = Mantissas[index % TimeSteps];
        for (int d = index / TimeSteps; d > 0; --d) v *= 10;
        return v;
    };
    constexpr int Last = 9 + 8 * TimeSteps; // 1 s.
    int below = 0; // The highest step at or below ns.
    while (below < Last && valueAt(below + 1) <= ns) ++below;
    const bool exact = valueAt(below) == ns;
    const int index = std::clamp(steps > 0 ? below + steps : below + steps + (exact ? 0 : 1), 0, Last);
    return uint32_t(std::clamp<uint64_t>(valueAt(index), scope::TriggerSettings::MinimumNs, scope::TriggerSettings::MaximumNs));
}
void TriggerEditor::adjust(unsigned row, int direction) {
    if (!direction) return;
    const int step = direction > 0 ? 1 : -1;
    switch (const Field f = field(row)) {
    case Source:
        if (enabled_[unsigned(settings_.channel)^1]) settings_.channel = scope::Channel(unsigned(settings_.channel)^1);
        break;
    case Slope: settings_.edge = scope::Edge(unsigned(settings_.edge) ^ 1); break;
    case Level: case Level2: {
        // The band's ends do not pass each other: High stays at or above Low.
        const bool two = settings_.twoLevels();
        const int low = f == Level && two ? std::max<int>(levelBounds_.low, levelValue(Level2)) : levelBounds_.low;
        const int high = f == Level2 ? std::min<int>(levelBounds_.high, levelValue(Level)) : levelBounds_.high;
        auto &level = levelField(f);
        level = int16_t(std::clamp(levelBounds_.clamp(level) + direction, std::min(low, high), high));
        break;
    }
    case Mode: settings_.mode = scope::TriggerMode((unsigned(settings_.mode) + 3 + step) % 3); break;
    case Type:
        settings_.type = TriggerType((unsigned(settings_.type) + 4 + step) % 4);
        if (settings_.type != TriggerType::Runt && settings_.condition == TriggerCondition::Any)
            settings_.condition = TriggerCondition::Greater; // Any is a runt's only.
        break;
    case Condition: {
        const unsigned n = options(row);
        const unsigned index = (option(row) + n + unsigned(step)) % n;
        settings_.condition = TriggerCondition(settings_.type == TriggerType::Runt ? (index + 1) % 5 : index + 1);
        if (windowed() && settings_.minimumNs >= settings_.maximumNs) settings_.maximumNs = stepTime(settings_.minimumNs, 1);
        break;
    }
    case Lower: case Upper: {
        auto &ns = f == Lower ? settings_.minimumNs : settings_.maximumNs;
        ns = stepTime(ns, direction);
        // Between and Outside keep the lower limit under the upper one.
        if (windowed() && settings_.minimumNs >= settings_.maximumNs)
            ns = f == Lower ? stepTime(settings_.maximumNs, -1) : stepTime(settings_.minimumNs, 1);
        break;
    }
    case Pretrigger: settings_.position=std::clamp<int>(int(settings_.position)+(direction>0 ? 25 : -25),0,1000); break;
    case Reject: settings_.reject = !settings_.reject; break;
    default: break;
    }
}
} // namespace openosc::ui
