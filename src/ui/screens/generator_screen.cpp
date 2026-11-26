#include "openosc/ui/screens/generator_screen.hpp"
#include "openosc/generator/capabilities.hpp"
#include "openosc/ui/scope/format.hpp"
#include <cstdio>

namespace openosc::ui {
namespace {
constexpr const char *Shapes[] = {"Sine", "Square", "Triangle", "Half sine", "Pos sine", "Sawtooth", "AWG"};
static_assert(sizeof Shapes / sizeof *Shapes == generator::ShapeCount);
const char *shapeName(generator::Shape shape) { return unsigned(shape) < generator::ShapeCount ? Shapes[unsigned(shape)] : "Invalid"; }
} // namespace
void GeneratorScreen::activate(const ApplicationState &) {
    host_.elements().text(ElementId::Details, "SET FREQUENCY"); // The frequency block's title, also on a rebuilt view.
    if (shown_) { update(snapshot_); return; } // The output as it is: never a placeholder OFF while ON.
    cards(); // Nothing shown yet: "--" and OFF.
    host_.parameters().select(1);
    host_.status("OFF", StatusTone::Neutral);
    host_.elements().text(ElementId::Reading, "--");
    host_.elements().text(ElementId::Unit, "Hz");
    host_.elements().text(ElementId::Measurements, "");
}
// F1 Wave, F2 Frequency (Unit in FREQUENCY: Hz/kHz/MHz), F3 Params, F4 Back from the list or Cancel
// of a field or the frequency; the open panel's card accented.
const SoftKeys &GeneratorScreen::keys(const GeneratorKeys &keys) {
    using Panel = GeneratorKeys::Panel;
    values();
    const char *rateLabel = snapshot_.requested.arbitrary() ? "Sample rate" : "Frequency";
    std::snprintf(unit_, sizeof unit_, "%s", keys.unit ? keys.unit : "");
    keys_ = {};
    keys_.keys[0] = {"Wave", shape_, true};
    keys_.keys[1] = keys.panel == Panel::Frequency ? SoftKey{"Unit", unit_, true} : SoftKey{rateLabel, frequency_, true};
    keys_.keys[2] = {"Params", params_, true};
    keys_.keys[3] = keys.panel == Panel::Frequency ? SoftKey{"Cancel", rateLabel, true}
                  : keys.editing ? SoftKey{"Cancel", "Field", true} : SoftKey{"Back", "Main", true};
    keys_.selected = keys.panel == Panel::Wave ? 0 : keys.panel == Panel::Frequency ? 1 : keys.panel == Panel::Params ? 2 : SoftKeys::None;
    return keys_;
}
void GeneratorScreen::status(const generator::Snapshot &snapshot) {
    // The ZT706S module answers asynchronously: SENDING while a frame is on the wire or waits for its
    // acknowledge, NO ACK when none came (the output state is then unknown).
    const bool error = snapshot.status == DataStatus::HardwareError, silent = snapshot.status == DataStatus::Timeout;
    const char *state = error ? "OUTPUT ERROR" : silent ? "NO ACK" : snapshot.status == DataStatus::Busy ? "SENDING"
                      : snapshot.enabled ? "ON" : "OFF";
    host_.status(state, error || silent ? StatusTone::Alert : snapshot.enabled ? StatusTone::Good : StatusTone::Neutral);
}
void GeneratorScreen::update(const generator::Snapshot &snapshot) {
    auto &elements = host_.elements();
    status(snapshot);
    // Arbitrary: the frequency its rate repeats the loaded points at (none loaded: "--").
    const auto &requested = snapshot.requested;
    const bool arbitrary = requested.arbitrary();
    elements.text(ElementId::Details, arbitrary ? "REPETITION RATE" : "SET FREQUENCY");
    const Rational hertz = !arbitrary ? requested.frequencyHz
                         : snapshot.arbitraryPoints ? Rational{requested.arbitrarySamplesPerSecond, snapshot.arbitraryPoints} : Rational{0, 0};
    const double set = hertz.value();
    const unsigned divisor = set >= 1000000 ? 1000000 : set >= 1000 ? 1000 : 1;
    const char *unit = divisor == 1000000 ? "MHz" : divisor == 1000 ? "kHz" : "Hz";
    char value[48] = "--", applied[32] = "--", text[112];
    const uint64_t denominator = uint64_t(hertz.denominator) * divisor;
    if (denominator) {
        const uint64_t fixed = uint64_t(hertz.numerator) * 100000 / denominator;
        std::snprintf(value, sizeof value, "%lu.%05lu", static_cast<unsigned long>(fixed / 100000),
                      static_cast<unsigned long>(fixed % 100000));
    }
    elements.text(ElementId::Reading, value);
    elements.text(ElementId::Unit, unit);
    if (snapshot.applied.actualHz.valid())
        plot::Format::quantity(snapshot.applied.actualHz.value(), "Hz", applied, sizeof applied);
    if (arbitrary && !snapshot.arbitraryPoints) std::snprintf(text, sizeof text, "No AWG waveform loaded");
    else if (arbitrary) {
        char rate[24];
        plot::Format::quantity(double(requested.arbitrarySamplesPerSecond), "S/s", rate, sizeof rate);
        std::snprintf(text, sizeof text, "Applied %s, %u pts at %s", applied, unsigned(snapshot.arbitraryPoints), rate);
    } else std::snprintf(text, sizeof text, "Applied %s", applied);
    elements.text(ElementId::Measurements, text);
    // Every preset alike: Applied in the frequency block, the level line under the preview's curve.
    snapshot_ = snapshot; shown_ = true;
    cards();
    host_.preview().update(snapshot.requested,
        host_.theme().elements[unsigned(MeasurementMode::Generator)][unsigned(ElementId::Preview)].color,
        snapshot.arbitrarySamples, snapshot.arbitraryPoints, snapshot.arbitraryRevision);
}
void GeneratorScreen::values() {
    const auto &s = snapshot_.requested;
    std::snprintf(shape_, sizeof shape_, "%s", shown_ ? shapeName(s.shape) : "--");
    if (shown_ && s.arbitrary()) plot::Format::quantity(double(s.arbitrarySamplesPerSecond), "S/s", frequency_, sizeof frequency_);
    else if (shown_) plot::Format::quantity(s.frequencyHz.value(), "Hz", frequency_, sizeof frequency_);
    const unsigned duty = s.dutyPermille; // 50.5 % as set, not cut to 50 %.
    if (!shown_) std::snprintf(params_, sizeof params_, "Fixed");
    else if (s.arbitrary() && snapshot_.arbitraryPoints) std::snprintf(params_, sizeof params_, "%u pts", unsigned(snapshot_.arbitraryPoints));
    else if (s.shape != generator::Shape::Square) generator::LevelText::name(s, params_, sizeof params_);
    else if (duty % 10) std::snprintf(params_, sizeof params_, "%u.%u%%", duty / 10, duty % 10);
    else std::snprintf(params_, sizeof params_, "%u%%", duty / 10);
}
void GeneratorScreen::cards() {
    values();
    auto &bar = host_.parameters();
    bar.item(0, "Wave", shape_);
    bar.item(1, snapshot_.requested.arbitrary() ? "Sample rate" : "Frequency", frequency_);
    bar.item(2, "Params", params_);
    bar.item(3, "Output", snapshot_.enabled ? "ON" : "OFF");
}
} // namespace openosc::ui
