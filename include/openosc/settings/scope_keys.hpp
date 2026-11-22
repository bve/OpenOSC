#pragma once
#include "openosc/board/current.hpp"
#include <cstdint>

namespace openosc {
// Settings > Display > Scope shortcuts (issue #96): what AUTO and the F2 and F4 holds do on the
// Scope waveform screen. Files before them: the defaults (Quick, Home view, Cursor settings).
struct ScopeKeys {
    // Quick: a tap starts Auto Setup (or cancels it), a 2 s hold enters Zoom & Pan. Classic: a tap
    // resets the pan (or cancels Auto Setup), a 2 s hold starts Auto Setup.
    enum class Auto : uint8_t { Quick, Classic, Count };
    // F2 hold: Home view (both zero lines centred, pan and zoom reset) or only the zoom and pan.
    enum class Reset : uint8_t { Home, ZoomPan, Count };
    // F4 hold: the editor it opens for review. Interleave only where the board has that control.
    enum class Shortcut : uint8_t { Cursors, Measurements, Acquisition, RecordMemory, Interleave, Count };
    Auto automatic = Auto::Quick;
    Reset reset = Reset::Home;
    Shortcut shortcut = Shortcut::Cursors;
    static constexpr bool available(Shortcut s) {
        return s < Shortcut::Count && (s != Shortcut::Interleave || board::Current::Features.interleaveControl());
    }
    // The destination in force: a saved one this board does not offer is Cursor settings.
    Shortcut destination() const { return available(shortcut) ? shortcut : Shortcut::Cursors; }
    bool valid() const { return automatic < Auto::Count && reset < Reset::Count && available(shortcut); }
    bool operator==(const ScopeKeys &o) const { return automatic == o.automatic && reset == o.reset && shortcut == o.shortcut; }
};
} // namespace openosc
