#pragma once
#include "openosc/acquisition/request.hpp"
#include "openosc/ui/scope/options.hpp"

namespace openosc::plot {
class Model;
// MOVE: the running trace moves through the analog zero of the next capture, a held record
// moves only on the screen, so its samples and metadata stay as captured.
class TraceMove {
public:
    struct Change { bool request = false, view = false; };
    // Pan steps one division of the shown window, one pixel when fine (PanStep without a model).
    static Change apply(ScopeRequest &request, Options &options, unsigned channel, bool held, int horizontal, int vertical, bool fine,
                        const Model *model = nullptr);
    static bool pan(Options &options, int horizontal, const Model *model = nullptr, bool fine = false);
};
// The factory X-Y entry: a continuous free-running picture, whatever the capture was doing before.
class XyMode {
public:
    static constexpr uint64_t MaximumTimebaseNs = 100000000; // Roll starts above this.
    static void enter(ScopeRequest &request, Options &options);
    static void constrain(const Options &options, ScopeRequest &request);
    static bool crossed(Mode before, Mode after) { return (before == Mode::XY) != (after == Mode::XY); }
};
} // namespace openosc::plot
