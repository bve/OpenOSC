#include "openosc/settings/theme_store.hpp"

namespace openosc {
bool ThemeStore::savePlot(const plot::Options &options) {
    if (!options.valid()) {
        error_ = "INVALID DISPLAY OPTIONS";
        return false;
    }
    if (!fs_.mounted()) {
        error_ = "STORAGE NOT AVAILABLE";
        return false;
    }
    auto candidate = theme_;
    candidate.plot = options;
    candidate.plot.centerHeldTrigger = false;
    if (!write(candidate)) {
        error_ = "CANNOT SAVE DISPLAY JSON";
        return false;
    }
    theme_ = candidate;
    error_ = nullptr;
    return true;
}
} // namespace openosc
