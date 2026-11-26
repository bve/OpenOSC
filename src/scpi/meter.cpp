#include "openosc/scpi/engine.hpp"
#include "openosc/data/decimal.hpp"
#include "openosc/measurement/dmm/key_command.hpp"
#include "openosc/measurement/dmm/range_stroke.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace openosc::scpi {
static_assert(dmm::RangeSelector::DeadlineMs < Engine::OperationMs &&
              dmm::FunctionSelector::DeadlineMs + dmm::FunctionSelector::PowerOnWaitMs < Engine::OperationMs,
              "A DMM operation ends before the SCPI wait for it");
// AUTO ON: its hold stroke (20, then 22 HoldGapMs later; each pair's transmission bounded at 100 ms by
// UartDma) and the confirmation after the 22.
static_assert(dmm::RangeStroke::HoldGapMs + 2 * 100 + dmm::KeyCommand::DeadlineMs < Engine::OperationMs,
              "AUTO's factory hold and its confirmation end before the SCPI wait for it");
namespace {
struct FunctionName { dmm::Function function; const char *name, *command; };
constexpr FunctionName functions[] = {
    {dmm::Function::VoltageDc,"VOLT","CONFigure:VOLTage:DC"}, {dmm::Function::VoltageAc,"VOLT AC","CONFigure:VOLTage:AC"},
    {dmm::Function::CurrentDc,"CURR","CONFigure:CURRent:DC"}, {dmm::Function::CurrentAc,"CURR AC","CONFigure:CURRent:AC"},
    {dmm::Function::Resistance,"RES","CONFigure:RESistance"}, {dmm::Function::Continuity,"CONT","CONFigure:CONTinuity"},
    {dmm::Function::Diode,"DIOD","CONFigure:DIODe"}, {dmm::Function::Capacitance,"CAP","CONFigure:CAPacitance"},
    {dmm::Function::MillivoltsDc,"MVOLT","CONFigure:MVOLtage:DC"}, {dmm::Function::MillivoltsAc,"MVOLT AC","CONFigure:MVOLtage:AC"},
    {dmm::Function::MilliampsDc,"MCURR","CONFigure:MCURrent:DC"}, {dmm::Function::MilliampsAc,"MCURR AC","CONFigure:MCURrent:AC"},
    {dmm::Function::Temperature,"TEMP","CONFigure:TEMPerature"}
};
}
void Engine::meterReading(bool secondary) {
    const auto &result = backend_.meter();
    const auto &value = secondary ? result.secondary : result.primary;
    if (backend_.meterStatus() == DataStatus::Stale || backend_.meterStatus() == DataStatus::NoData) error(-230, true);
    else if (!secondary && value.status == DataStatus::Overload) reply("1E+9"); // OWON overflow sentinel.
    else if (secondary && (!secondary_ || !value.valid())) reply("NON");
    else if (!value.valid()) error(-230, true);
    else number(value.si);
}
// SYSTem:DMM:HOLD:<field>: the hold settings one at a time, each checked as the whole by
// HoldSettings::valid (the editor's and the file's check; a value out of its limits, off its step or
// breaking rearm > band: -222, nothing changes). Times in ms, band and rearm in last-digit units.
bool Engine::meterHoldCommand(const Command &c) {
    const auto match = [&](const char *pattern) { return Header::matches(c.header, pattern); };
    struct Field { const char *header; uint32_t dmm::HoldSettings::*value; };
    static constexpr Field fields[] = {{"SYSTem:DMM:HOLD:DELay", &dmm::HoldSettings::delayMs}, {"SYSTem:DMM:HOLD:STABle", &dmm::HoldSettings::stableMs},
        {"SYSTem:DMM:HOLD:BAND", &dmm::HoldSettings::bandDigits}, {"SYSTem:DMM:HOLD:REARm", &dmm::HoldSettings::rearmDigits},
        {"SYSTem:DMM:HOLD:COUNt", &dmm::HoldSettings::minReadings}};
    static constexpr const char *modes[] = {"MANual", "DELayed", "STABle", "AUTO"}, *replies[] = {"MAN", "DEL", "STAB", "AUTO"};
    static_assert(sizeof modes / sizeof *modes == unsigned(dmm::HoldMode::Count));
    auto settings = backend_.meterHold().settings;
    const auto apply = [&] {
        if (!settings.valid()) error(-222);
        else if (!backend_.meterHold(settings)) error(-221);
    };
    if (match("SYSTem:DMM:HOLD:MODE")) {
        if (c.query) { reply("%s", replies[unsigned(settings.mode)]); return true; }
        for (unsigned i = 0; i < unsigned(dmm::HoldMode::Count); ++i)
            if (Header::word(c.argument, modes[i])) { settings.mode = dmm::HoldMode(i); apply(); return true; }
        error(-224);
        return true;
    }
    for (const auto &f : fields) {
        if (!match(f.header)) continue;
        if (c.query) { reply("%lu", (unsigned long)(settings.*f.value)); return true; }
        double value;
        if (!argument(c, value)) return true;
        if (!(value >= 0 && value <= 100000) || value != double(uint32_t(value))) { error(-222); return true; }
        settings.*f.value = uint32_t(value);
        apply();
        return true;
    }
    if (match("SYSTem:DMM:HOLD:ARM")) {
        if (c.query || *c.argument) error(-108, c.query);
        else if (!backend_.armMeterHold()) error(-221);
        return true;
    }
    if (match("SYSTem:DMM:HOLD:STATe")) {
        // phase,captured,captures,remaining_ms: IDLE|DEL|WAIT|HELD; a snapshot in meter() (0/1); the
        // captures since boot; the countdown left (Delayed).
        if (!c.query) { error(-109); return true; }
        static constexpr const char *phases[] = {"IDLE", "DEL", "WAIT", "HELD"};
        const auto hold = backend_.meterHold();
        reply("%s,%u,%lu,%lu", phases[unsigned(hold.phase)], unsigned(hold.captured), (unsigned long)hold.captures,
              (unsigned long)hold.remainingMs);
        return true;
    }
    return false;
}
bool Engine::meterCommand(const Command &c) {
    const auto match = [&](const char *pattern) { return Header::matches(c.header, pattern); };
    if (meterHoldCommand(c)) return true;
    if (match("SYSTem:DMM:TEMPerature:CJUNction")) {
        auto settings = backend_.meterTemperature();
        if (c.query) {
            if (settings.manualConfigured()) number(settings.junctionCelsius());
            else error(-230, true);
        } else if (Header::word(c.argument, "NONE")) {
            settings.junctionDeciC = dmm::TemperatureSettings::Unset;
            settings.source = dmm::JunctionSource::Manual;
            if (!backend_.meterTemperature(settings)) error(-221);
        } else {
            double value;
            if (argument(c, value)) {
                const double deciC = std::round(value * 10);
                if (!std::isfinite(deciC) || deciC < dmm::TemperatureSettings::Minimum || deciC > dmm::TemperatureSettings::Maximum ||
                    std::fabs(value * 10 - deciC) > 1e-8) error(-222);
                else {
                    settings.junctionDeciC = int32_t(deciC);
                    settings.source = dmm::JunctionSource::Manual;
                    if (!backend_.meterTemperature(settings)) error(-221);
                }
            }
        }
        return true;
    }
    if (match("SYSTem:DMM:TEMPerature:UNIT")) {
        auto settings = backend_.meterTemperature();
        if (c.query) reply("%s", settings.fahrenheit ? "F" : "C");
        else if (Header::word(c.argument, "C") || Header::word(c.argument, "F")) {
            settings.fahrenheit = Header::word(c.argument, "F");
            if (!backend_.meterTemperature(settings)) error(-221);
        } else error(-224);
        return true;
    }
    if (match("SYSTem:DMM:TEMPerature:TYPE")) {
        auto settings = backend_.meterTemperature();
        if (c.query) reply("%s", dmm::Thermocouple::letter(settings.type));
        else {
            unsigned index = 0;
            while (index < unsigned(dmm::ThermocoupleType::Count) && !Header::word(c.argument, dmm::Thermocouple::letter(dmm::ThermocoupleType(index)))) ++index;
            if (index == unsigned(dmm::ThermocoupleType::Count)) error(-224);
            else {
                settings.type = dmm::ThermocoupleType(index);
                if (!backend_.meterTemperature(settings)) error(-221);
            }
        }
        return true;
    }
    if (match("SYSTem:DMM:PACKet")) { // The newest raw packet in hex, for decoding annunciators.
        const uint8_t *data = nullptr;
        const size_t bytes = backend_.meterPacket(data);
        char text[2 * 18 + 1] = "";
        for (size_t i = 0; i < bytes && i < 18; ++i) std::snprintf(text + 2 * i, 3, "%02X", data[i]);
        if (!c.query) error(-109); else if (!bytes) error(-230, true); else reply("%s", text);
        return true;
    }
    if (match("SYSTem:DMM:RESult")) {
        if (!c.query) { error(-109); return true; }
        const auto &result = backend_.meter();
        const auto health = backend_.meterStatus();
        char primary[56] = "NAN", secondary[56] = "NAN", timestamp[24];
        if (health == DataStatus::Ok && result.primary.valid()) Decimal::number(result.primary.si,primary,sizeof primary);
        if (secondary_ && health != DataStatus::Stale && health != DataStatus::NoData && result.secondary.valid()) Decimal::number(result.secondary.si,secondary,sizeof secondary);
        Decimal::integer(result.timestampUs,timestamp,sizeof timestamp);
        unsigned index = 0; const bool knownRange = dmm::Ranges::current(result,&index);
        reply("%u,%s,%u,%s,%u,%u,%d,%u,%u,%lu,%s", unsigned(dmm::Functions::identify(result)),primary,
              unsigned(health),secondary,unsigned(secondary_ ? result.secondary.quantity : Quantity::Unknown),knownRange ? index+1 : 0,
              result.range == dmm::RangeMode::Unknown ? -1 : result.range == dmm::RangeMode::Auto,
              unsigned(result.packetHold),unsigned(result.peerRelative),(unsigned long)result.sequence,timestamp);
    } else if (match("SYSTem:DMM:HOLD") || match("SYSTem:DMM:RELative")) {
        const bool hold = match("SYSTem:DMM:HOLD");
        if (c.query) { const auto &r = backend_.meter(); reply("%u", unsigned(hold ? r.packetHold : r.peerRelative)); }
        else { bool value; if (boolean(c, value) && !(hold ? backend_.holdMeter(value) : backend_.relativeMeter(value))) error(-221); }
    } else if (match("SYSTem:DMM:STATistics")) {
        // count,minimum,maximum,average,quantity,sequence: the screen's MIN/MAX/AVG (one Session
        // period, local REL applied as on the screen), SI; none of the shown quantity: 0,NAN...
        if (!c.query) { error(-109); return true; }
        const auto s = backend_.meterStatistics();
        char values[3][56] = {"NAN","NAN","NAN"}, count[24];
        if (s.valid) {
            Decimal::number(s.minimum,values[0],sizeof values[0]); Decimal::number(s.maximum,values[1],sizeof values[1]);
            Decimal::number(s.average,values[2],sizeof values[2]);
        }
        Decimal::integer(s.valid ? s.count : 0,count,sizeof count);
        reply("%s,%s,%s,%s,%u,%lu",count,values[0],values[1],values[2],unsigned(s.quantity),(unsigned long)backend_.meter().sequence);
    } else if (match("SYSTem:DMM:STATistics:RESet")) {
        if (c.query || *c.argument) error(-108, c.query); else backend_.resetMeterStatistics();
    } else if (match("SYSTem:DMM:RANGe:LIST")) {
        // The full scales (SI) of the current function, in RANGE index order: count,max1,max2...
        if (!c.query) { error(-109); return true; }
        unsigned count = 0; const auto *ranges = dmm::Ranges::list(dmm::Functions::identify(backend_.meter()), count);
        if (!ranges) { reply("0"); return true; }
        char text[sizeof response_]; size_t used = size_t(std::snprintf(text,sizeof text,"%u",count));
        for (unsigned i = 0; i < count && used < sizeof text; ++i) {
            char value[56]; Decimal::number(ranges[i].maximumSi,value,sizeof value);
            used += size_t(std::snprintf(text+used,sizeof text-used,",%s",value));
        }
        reply("%s", text);
    } else if (match("FUNCtion")) {
        const auto actual = dmm::Functions::identify(backend_.meter());
        if (c.query) {
            for (const auto &f : functions) if (f.function == actual) {
                // OWON has no mV/mA function names; keep the physical quantity truthful.
                const char *name = f.name;
                if (actual == dmm::Function::MillivoltsDc) name = "VOLT";
                if (actual == dmm::Function::MillivoltsAc) name = "VOLT AC";
                if (actual == dmm::Function::MilliampsDc) name = "CURR";
                if (actual == dmm::Function::MilliampsAc) name = "CURR AC";
                reply("\"%s\"", name); return true;
            }
            error(-230, true);
        } else error(-108);
    } else if (match("FUNC2")) {
        if (c.query) reply("\"%s\"", secondary_ && backend_.meter().secondary.quantity == Quantity::Frequency ? "FREQ" : "NONe");
        else if (Header::word(c.argument, "FREQuency")) secondary_ = true;
        else if (Header::word(c.argument, "NONE")) secondary_ = false;
        else error(-224);
    } else if (match("AUTO")) {
        if (c.query) { if (backend_.meter().range == dmm::RangeMode::Unknown) error(-230, true); else reply("%u", backend_.meter().range == dmm::RangeMode::Auto); }
        else { bool value = true; if ((!*c.argument || boolean(c, value))) { if (!backend_.setAuto(value)) error(-221); else waitOperation(); } }
    } else if (match("RANGe")) {
        if (c.query) { unsigned index; if (!dmm::Ranges::current(backend_.meter(), &index)) error(-230, true); else reply("%u", index + 1); }
        else { double value; if (argument(c, value)) {
            if (value < 1 || value > 8 || value != unsigned(value)) error(-222);
            else if (!backend_.setRange(unsigned(value) - 1)) error(-221); else waitOperation(); } }
    } else if (match("RATE")) {
        if (c.query) reply("F"); else if (!Header::word(c.argument, "F")) error(-224);
    } else if (match("MEAS1") || match("MEASure") || match("FETCh")) {
        if (!c.query) error(-109); else meterReading(false);
    } else if (match("MEAS2")) { if (!c.query) error(-109); else meterReading(true); }
    else if (match("READ")) {
        if (!c.query) error(-109);
        else if (backend_.meter().packetHold) error(-221, true);
        else { waitingReading_ = true; readingSequence_ = backend_.meter().sequence; deadline_ = now_ + 2000; }
    } else {
        for (const auto &f : functions) if (match(f.command)) {
            if (c.query || *c.argument) error(-108, c.query);
            else if (!backend_.selectFunction(f.function)) error(-221); else waitOperation();
            return true;
        }
        return false;
    }
    return true;
}
} // namespace openosc::scpi
