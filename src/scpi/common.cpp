#include "openosc/scpi/engine.hpp"
#include "openosc/data/decimal.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace openosc::scpi {
bool Engine::common(const Command &c) {
    const auto match = [&](const char *pattern) { return Header::matches(c.header, pattern); };
    if (match("*IDN")) {
        if (!c.query || *c.argument) error(-108, c.query);
        else reply("OpenOSC,%s,%s,%s", backend_.multimeter() ? "XDM1041" : "DS1052", backend_.serial(), backend_.version());
    } else if (match("*CLS")) { if (c.query || *c.argument) error(-108, c.query); else errors_.clear(); }
    else if (match("*OPC") || match("*WAI")) {
        if (*c.argument || (match("*WAI") && c.query)) error(-108,c.query);
        else {
            completion_ = match("*WAI") ? Completion::Wait : c.query ? Completion::Query : Completion::Event;
            const uint32_t sweep = backend_.multimeter() ? 0 : backend_.scopeRequest().timebaseNs/100000;
            deadline_ = now_ + std::max<uint32_t>({10000,sweep+2000,backend_.operationTimeoutMs()});
        }
    }
    else if (match("*ESR")) { if (c.query) reply("%u", errors_.readEvent()); else error(-109); }
    else if (match("*STB")) { if (c.query) reply("%u", statusByte()); else error(-109); }
    else if (match("*ESE") || match("*SRE")) {
        auto &target = match("*ESE") ? ese_ : sre_;
        if (c.query) reply("%u", target);
        else { double value; if (argument(c, value)) { if (value < 0 || value > 255 || value != unsigned(value)) error(-222); else target = unsigned(value); } }
    } else if (match("SYSTem:ERRor") || match("SYSTem:ERRor:NEXT")) {
        if (!c.query) error(-109); else { const auto code = errors_.pop(); reply("%d,\"%s\"", code, ErrorQueue::message(code)); }
    } else if (match("SYSTem:VERSion")) { if (c.query) reply("1999.0"); else error(-109); }
    else if (match("SYSTem:REMote")) {
        if (c.query) reply("%u", backend_.remote());
        else { bool value; if (boolean(c, value) && !backend_.remote(value)) error(-221); }
    } else if (match("SYSTem:PERFormance")) {
        PerformanceSnapshot p;
        if (!c.query || *c.argument) error(-108,c.query);
        else if (!backend_.performance(p)) error(-230,true);
        else {
            char elapsed[24], ui[24];
            Decimal::integer(p.elapsedCycles,elapsed,sizeof elapsed); Decimal::integer(p.uiCycles,ui,sizeof ui);
            reply("%lu,%lu,%lu,%lu,%lu,%s,%s",static_cast<unsigned long>(p.uptimeMs),static_cast<unsigned long>(p.coreHz),
                static_cast<unsigned long>(p.captures),static_cast<unsigned long>(p.projections),
                static_cast<unsigned long>(p.flushes),elapsed,ui);
        }
    } else if (match("SYSTem:MEMory")) {
        // valid,total,used,peak,free,largest_free,fragmentation_pct (bytes): the GUI heap, walked once
        // per query. Without one (headless, display not initialized): 0 and NAN, nothing invented.
        MemorySnapshot m;
        if (!c.query || *c.argument) error(-108,c.query);
        else if (!backend_.memory(m)) reply("0,NAN,NAN,NAN,NAN,NAN,NAN");
        else reply("1,%lu,%lu,%lu,%lu,%lu,%u",static_cast<unsigned long>(m.total),static_cast<unsigned long>(m.used),
                   static_cast<unsigned long>(m.peak),static_cast<unsigned long>(m.free),
                   static_cast<unsigned long>(m.largestFree),unsigned(m.fragmentationPct));
    } else if (match("DISPlay:DATA") || match("HCOPy:DATA")) {
        if (!c.query || *c.argument) error(-108,c.query);
        else if (!backend_.screenCapture()) error(-200,true);
        else {
            char count[16]; std::snprintf(count,sizeof count,"%lu",(unsigned long)ScreenBytes);
            reply("#%u%s",unsigned(std::strlen(count)),count);
            screenBinary_ = screenLease_ = true; screenAt_ = 0;
        }
    } else if (match("SYSTem:STALl")) {
        char text[sizeof response_ - 1]; // Whole entries that fit the reply (StallLog::text).
        if (!c.query || *c.argument) error(-108,c.query);
        else { backend_.stalls(text,sizeof text); reply("%s",text); }
    } else if (match("SYSTem:BOOT")) { // ms at start,mounted,settings,guard,clocks,data,display,view,frame,crc,nor,disk,fat; ,ltdc:kept/registers.
        char text[Backend::BootTextBytes];
        if (!c.query || *c.argument) error(-108,c.query);
        else { backend_.bootTimes(text,sizeof text); reply("%s",text); }
    } else if (match("SYSTem:CALibration:FACTory")) {
        // valid,identity_crc,gainword1,gainword2,factory_scale1,factory_scale2: the record alone (docs/SCPI.md).
        Backend::FactoryCalibration f;
        if (!c.query || *c.argument) error(-108,c.query);
        else if (!backend_.factoryCalibration(f)) error(-230,true);
        else reply("%u,%u,%u,%u,%u,%u",unsigned(f.valid),unsigned(f.identity),unsigned(f.gainWord[0]),unsigned(f.gainWord[1]),
                   unsigned(f.scale[0]),unsigned(f.scale[1]));
    } else if (match("SYSTem:CALibration:FACTory:FORMat")) {
        // active_boot_policy,resolved_format (docs/SCPI.md).
        Backend::FactoryRecordReading f;
        if (!c.query || *c.argument) error(-108,c.query);
        else if (!backend_.factoryRecordFormat(f)) error(-230,true);
        else reply("%s,%s",f.policy,f.format);
    } else if (match("SYSTem:CLOCk:MODE")) {
        if (c.query) { if (*c.argument) error(-108,true); else reply("%lu", static_cast<unsigned long>(backend_.coreMhz())); }
        else {
            double value;
            if (argument(c, value)) {
                if (value <= 0 || value > 1000 || value != unsigned(value)) error(-224);
                else if (const int code = backend_.coreMhz(unsigned(value))) error(code);
            }
        }
    } else if (match("SYSTem:MODE")) {
        // The MODE screen; a different measurement reconnects USB (the reply of this session is lost).
        static constexpr const char *names[] = {"SCOP","DMM","GEN"}, *words[] = {"SCOPe","DMM","GENerator"};
        static_assert(sizeof names / sizeof *names == InstrumentModeCount);
        if (c.query) reply("%s", names[unsigned(backend_.screen())]);
        else {
            unsigned mode = 0;
            while (mode < InstrumentModeCount && !Header::word(c.argument, words[mode])) ++mode;
            if (mode == InstrumentModeCount) error(*c.argument ? -224 : -109);
            else if (!backend_.screen(MeasurementMode(mode))) error(-221);
        }
    } else if (match("SYSTem:RECovery")) {
        if (c.query || *c.argument) error(-108,c.query);
        else if (!backend_.recovery()) error(-221);
    } else return false;
    return true;
}
} // namespace openosc::scpi
