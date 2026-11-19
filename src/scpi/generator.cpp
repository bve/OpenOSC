#include "openosc/scpi/engine.hpp"
#include "openosc/generator/capabilities.hpp"
#include "openosc/generator/frequency.hpp"
#include "openosc/generator/module_link.hpp"
#include <cmath>

namespace openosc::scpi {
static_assert(generator::ModuleLink::ExchangeMs < Engine::GeneratorMs, "A module exchange ends before the SCPI wait");
bool Engine::generatorCommand(const Command &c) {
    const auto match = [&](const char *pattern) { return Header::matches(c.header, pattern); };
    const bool shape = match("SOURce:FUNCtion"), frequency = match("SOURce:FREQuency"), duty = match("SOURce:PULSe:DCYCle"), output = match("OUTPut");
    const bool level=match("SOURce:LEVel:PROFile"), low=match("SOURce:LEVel:LOW"), high=match("SOURce:LEVel:HIGH");
    const bool amplitude = match("SOURce:VOLTage");
    // ZT703S: the Dedicated square's drive (FACTory: DAC below 1 kHz, PWM from 1 kHz; PWM always) and
    // the path the output takes now (DAC or PWM, the one decision Levels::pwm()).
    const bool drive = match("SOURce:SQUare:DRIVe"), path = match("SOURce:LEVel:PATH");
    if (match("OUTPut:STARtup")) { // Startup output: OFF (default) or RESTore the last confirmed state.
        if (c.query) reply("%s", backend_.generatorStartup() ? "REST" : "OFF");
        else if (Header::word(c.argument, "OFF")) { if (!backend_.generatorStartup(false)) error(-200); }
        else if (Header::word(c.argument, "RESTore")) { if (!backend_.generatorStartup(true)) error(-200); }
        else error(-224);
        return true;
    }
    if (!shape && !frequency && !duty && !output && !level && !low && !high && !amplitude && !drive && !path) return false;
    using Caps = generator::Capabilities;
    // Level profiles and DAC codes are the ZT703S DAC's; a volt amplitude is the ZT706S module's.
    if (((level || low || high || drive || path) && !Caps::Levels) || (amplitude && !Caps::Amplitude)) { error(-241, c.query); return true; }
    if (path && !c.query) { error(-113); return true; } // PATH is a query only.
    const auto snapshot = backend_.generator();
    auto settings = snapshot.requested;
    if (c.query) {
        if (output) reply("%u", backend_.generator().enabled);
        else if (frequency) number(settings.frequencyHz.value());
        else if (duty) number(settings.dutyPermille / 10.);
        else if (amplitude) number(settings.amplitudeMillivolts / 1000.);
        else if (level) reply("%s",generator::Levels::Names[unsigned(settings.level)]);
        else if (drive) reply("%s", settings.squareDrive == generator::SquareDrive::Pwm ? "PWM" : "FACT");
        else if (path) reply("%s", generator::Levels::pwm(settings) ? "PWM" : "DAC");
        else if (low || high) { const auto codes=generator::Levels::codes(settings); reply("%u",low ? codes.minimum : codes.maximum); }
        else { constexpr const char *names[] = {"SIN","SQU","TRI","HALF","POS","RAMP"}; reply("%s", names[unsigned(settings.shape)]); }
        return true;
    }
    if (output) {
        bool value;
        if (boolean(c, value)) { if (!backend_.output(value)) error(-200); else waitGenerator(); }
        return true;
    }
    if (drive) {
        if (Header::word(c.argument, "FACTory")) settings.squareDrive = generator::SquareDrive::Factory;
        else if (Header::word(c.argument, "PWM")) settings.squareDrive = generator::SquareDrive::Pwm;
        else { error(-224); return true; }
    } else if (level) {
        constexpr const char *names[]={"DEDicated","LOW","HIGH","CUSTom"};
        unsigned index=0; while (index<4 && !Header::word(c.argument,names[index])) ++index;
        if (index==4) { error(-224); return true; }
        settings.level=generator::Level(index);
    } else if (low || high) {
        double value; if (!argument(c,value)) return true;
        if (value<0 || value>4095 || value!=std::floor(value)) { error(-222); return true; }
        const auto codes=generator::Levels::codes(settings);
        settings.minimumCode=low ? uint16_t(value) : codes.minimum;
        settings.maximumCode=high ? uint16_t(value) : codes.maximum;
        settings.level=generator::Level::Custom;
        if (!settings.valid()) { error(-222); return true; }
    } else if (shape) {
        constexpr const char *names[] = {"SINusoid","SQUare","TRIangle","HALF","POSitive","RAMP"};
        unsigned i = 0; while (i < 6 && !Header::word(c.argument, names[i])) ++i;
        if (i == 6) { error(-224); return true; } settings.shape = generator::Shape(i);
    } else if (frequency) {
        double value; if (!argument(c, value, Header::Unit::Frequency)) return true;
        const double millihertz = std::round(value * 1000);
        if (!(millihertz >= double(Caps::MinimumMillihertz)) || millihertz > double(Caps::maximumMillihertz(settings.shape,settings.level,snapshot.timerHz))) {
            error(-222); return true;
        }
        settings.frequencyHz = generator::Frequency::rational(uint64_t(millihertz));
    } else if (amplitude) {
        double value; if (!argument(c, value, Header::Unit::Voltage)) return true; // 2, 2V, 2000mV.
        const long millivolts = std::lround(value * 1000);
        if (millivolts < 0 || millivolts % generator::ModuleCapabilities::AmplitudeStepMillivolts ||
            millivolts > Caps::amplitudeLimitMillivolts(settings.shape, generator::Frequency::millihertz(settings.frequencyHz))) {
            error(-222); return true;
        }
        settings.amplitudeMillivolts = uint16_t(millivolts);
    } else {
        double value; if (!argument(c, value, Header::Unit::Percent)) return true;
        const long permille = std::lround(value * 10);
        if (!Caps::duty(settings.shape) || permille < Caps::MinimumDutyPermille || permille > Caps::MaximumDutyPermille) { error(-222); return true; }
        settings.dutyPermille = uint16_t(permille);
    }
    // The shape, frequency and amplitude together (e.g. 10 MHz sine to triangle: 2 MHz): a conflict
    // with what is set is refused, nothing changes.
    if (!Caps::accepts(settings, snapshot.timerHz)) { error(-221); return true; } // At the clock that runs (DAC limit).
    if (!backend_.configureGenerator(settings)) error(-222);
    else waitGenerator();
    return true;
}
} // namespace openosc::scpi
