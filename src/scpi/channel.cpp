#include "openosc/scpi/engine.hpp"
#include "openosc/acquisition/probe_attenuation.hpp"
#include <cmath>

namespace openosc::scpi {
bool Engine::channelCommand(const Command &c) {
    unsigned channel = 0;
    const auto match = [&](const char *pattern) { return Header::matches(c.header, pattern, &channel); };
    const bool display = match("CHANnel#:DISPlay"), coupling = match("CHANnel#:COUPling"),
        probe = match("CHANnel#:PROBe"), scale = match("CHANnel#:SCALe"), offset = match("CHANnel#:OFFSet"),
        bandwidth = match("CHANnel#:BWL"), range = match("CHANnel#:RANGe"), counter=match("CHANnel#:FCNT");
    if (!display && !coupling && !probe && !scale && !offset && !bandwidth && !range && !counter) return false;
    if (channel < 1 || channel > 2) { error(-114, c.query); return true; }
    --channel;
    auto r = backend_.scopeRequest(); auto &f = r.frontend;
    const double factor = double(r.probeNumerator[channel]) / r.probeDenominator[channel];
    const double volts = scope::VoltageRanges::millivolts(f.range[channel]) / 1000. * factor;
    if (counter) {
        scope::CounterReading reading;
        if (!c.query) error(-109);
        else if (!backend_.counter(channel,reading)) error(-230,true);
        else number(reading.hertz);
    } else if (display) {
        if (c.query) reply("%u", backend_.channelShown(channel));
        else { bool value; if (boolean(c, value)) {
            // The panel's rule: the last channel shown off only with the DMM trace on; the trigger follows.
            backend_.releaseFrame();
            preambleChannels_ = 0;
            if (const int16_t code = backend_.showChannel(channel, value)) error(code);
        } }
    } else if (coupling) {
        if (c.query) reply("%s", f.coupling[channel] == scope::Coupling::Ac ? "AC" : "DC");
        else { if (Header::word(c.argument, "AC")) f.coupling[channel] = scope::Coupling::Ac;
            else if (Header::word(c.argument, "DC")) f.coupling[channel] = scope::Coupling::Dc;
            else { error(-224); return true; } apply(r); }
    } else if (probe) {
        if (c.query) number(factor);
        else { double value; if (argument(c, value)) {
            if (!scope::ProbeAttenuation::contains(value)) error(-222);
            else { r.probeNumerator[channel] = unsigned(value); r.probeDenominator[channel] = 1; apply(r); } } }
    } else if (scale || range) {
        if (c.query) number(volts * (range ? 8 : 1));
        else { double value; if (argument(c, value, Header::Unit::Voltage)) {
            unsigned index; const double mv = value / factor * 1000 / (range ? 8 : 1);
            if (mv < 1 || mv > 10000 || std::abs(mv - std::round(mv)) > 1e-6 || !scope::VoltageRanges::index(std::lround(mv), index)) error(-222);
            else { f.range[channel] = index; apply(r); } } }
    } else if (offset) {
        if (c.query) number((80 - f.position[channel]) * volts / 20);
        else { double value; if (argument(c, value, Header::Unit::Voltage)) {
            const double pos = 80 - value * 20 / volts;
            if (pos < 0 || pos > 160) error(-222); else { f.position[channel] = std::lround(pos); apply(r); } } }
    } else {
        if (c.query) reply("OFF"); else if (!Header::word(c.argument, "OFF")) error(-224);
    }
    return true;
}
} // namespace openosc::scpi
