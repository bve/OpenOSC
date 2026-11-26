#include "openosc/ui/screens/scope_screen.hpp"
#include <cstring>

namespace openosc::ui {
void ScopeScreen::status(DataStatus health, bool held) {
    health_ = health; statusHeld_ = held;
    const bool newFrame = newFrame_;
    newFrame_ = false;
    const auto &model = host_.waveform().model();
    const auto &m = model.metadata();
    const bool calibrated = model.hasFrame() && (!m.channels[0].enabled || m.channels[0].calibrated) &&
                            (!m.channels[1].enabled || m.channels[1].calibrated);
    const char *text = held ? calibrated ? "STOP" : "STOP RAW" : calibrated ? "RUN" : "RUN RAW";
    if (!held && model.hasFrame() && m.rolling) text=calibrated ? "ROLL" : "ROLL RAW";
    else if (!held && model.hasFrame() && triggerMode_ == scope::TriggerMode::Auto) {
        // Auto: TRIG'D from a triggered frame until TrigdHoldMs without one, then AUTO (forced sweeps).
        // Only a new frame stamps it: a redraw for another reason (trigger mode, WAIT, AUTO SETUP)
        // must not keep an old triggered frame's TRIG'D alive.
        const uint32_t now = lv_tick_get();
        if (m.triggered && newFrame) { trigd_ = true; trigdAt_ = now; }
        else if (trigd_ && now - trigdAt_ >= TrigdHoldMs) trigd_ = false;
        text = trigd_ ? calibrated ? "TRIG'D" : "TRIG'D RAW" : calibrated ? "AUTO" : "AUTO RAW";
    } else if (!held && model.hasFrame() && triggerMode_ == scope::TriggerMode::Normal && m.triggered)
        text = calibrated ? "TRIG'D" : "TRIG'D RAW"; // Armed with none: WAIT below.
    if (waiting_) text = triggerMode_ == scope::TriggerMode::Single ? "SGL WAIT" : "WAIT";
    if (held && triggerMode_ == scope::TriggerMode::Single && m.triggered) text = "SINGLE";
    switch (health) {
    case DataStatus::NoData: text = "WAITING FOR DATA"; break;
    case DataStatus::Timeout: text = "CAPTURE TIMEOUT"; break;
    case DataStatus::HardwareError: text = "CAPTURE ERROR"; break;
    case DataStatus::Uncalibrated: text = "CALIBRATION ERROR"; break;
    case DataStatus::Overflow: text=m.rolling ? "ROLL / GAP" : "CAPTURE OVERFLOW"; break;
    case DataStatus::InvalidArgument:
    case DataStatus::Unsupported: text = "CAPTURE CONFIG ERROR"; break;
    default: break;
    }
    if (automatic_ && (health==DataStatus::Ok || health==DataStatus::NoData)) text="AUTO SETUP";
    if (calibrating_ && (health==DataStatus::Ok || health==DataStatus::NoData)) text="CALIBRATING";
    // A trend on show (generator()): ROLL is TREND, and it and STOP carry the record's elapsed time in a
    // fixed width ("TREND 03:59", "STOP 1:02:15"), so the pill does not jump as the seconds count.
    char trend[24];
    if (trendSeconds_ >= 0 && (!std::strcmp(text,"ROLL") || !std::strcmp(text,"ROLL RAW") || !std::strcmp(text,"STOP") ||
                               !std::strcmp(text,"STOP RAW"))) {
        const auto s = unsigned(trendSeconds_);
        const char *state = text[0] == 'R' ? text[4] ? "TREND RAW" : "TREND" : text;
        if (s >= 3600) std::snprintf(trend, sizeof trend, "%s %u:%02u:%02u", state, s / 3600, s / 60 % 60, s % 60);
        else std::snprintf(trend, sizeof trend, "%s %02u:%02u", state, s / 60, s % 60);
        text = trend;
    }
    if (std::strcmp(status_, text)) {
        std::snprintf(status_, sizeof status_, "%s", text);
        const bool running = !std::strncmp(text,"RUN",3) || !std::strncmp(text,"TRIG'D",6) || !std::strncmp(text,"TREND",5) ||
                             (!std::strncmp(text,"ROLL",4) && !std::strchr(text,'/'));
        const bool stopped = !std::strncmp(text,"STOP",4) || !std::strcmp(text,"SINGLE") || std::strstr(text,"ERROR") ||
                             std::strstr(text,"TIMEOUT") || std::strstr(text,"OVERFLOW");
        host_.status(text, running ? StatusTone::Good : stopped ? StatusTone::Alert : StatusTone::Warning);
    }
}
} // namespace openosc::ui
