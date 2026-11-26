#include "openosc/scpi/engine.hpp"
#include "openosc/data/decimal.hpp"
#include "openosc/acquisition/record_depths.hpp"
#include "openosc/acquisition/timebases.hpp"
#include <algorithm>
#include <cmath>

namespace openosc::scpi {
bool Engine::scopeCommand(const Command &c) {
    if (channelCommand(c) || waveformCommand(c)) return true;
    const auto match = [&](const char *pattern) { return Header::matches(c.header, pattern); };
    auto request = backend_.scopeRequest();
    if (match("MEASure:FCNT:QUALity") || match("MEASure:COUNter:QUALity")) {
        scope::CounterReading reading;
        static constexpr const char *Names[] = {"CLEAN", "FILTERED", "UNVERIFIED", "UNCERTAIN", "CHANGING"};
        if (!c.query) error(-109);
        else if (!backend_.counter(unsigned(request.trigger.channel),reading)) error(-230,true);
        else reply("%s,%u", Names[unsigned(reading.quality)], unsigned(reading.filter));
    } else if (match("MEASure:FCNT") || match("MEASure:COUNter")) {
        scope::CounterReading reading;
        if (!c.query) error(-109);
        else if (!backend_.counter(unsigned(request.trigger.channel),reading)) error(-230,true);
        else number(reading.hertz);
    } else if (match("RUN") || match("SINGle") || match("STOP")) {
        if (c.query || *c.argument) error(-108, c.query);
        else { backend_.releaseFrame(); if (match("STOP")) backend_.stop(); else if (!backend_.run(match("SINGle"))) error(-221); }
    } else if (match("TFORce") || match("TRIGger:FORCe")) {
        if (c.query || *c.argument) error(-108, c.query);
        else if (!backend_.forceTrigger()) error(-221);
    } else if (match("ACQuire:FRAMe")) {
        if (!c.query || *c.argument) error(-108, c.query);
        else reply("%lu", (unsigned long)backend_.frameSequence());
    } else if (match("AUToscale")) {
        if (c.query || *c.argument) error(-108,c.query);
        else { backend_.releaseFrame(); if (!backend_.autoScale()) error(-221); }
    } else if (match("TRIGger:STATus")) {
        if (!c.query) error(-109);
        else reply("%s", backend_.held() ? "STOP" : backend_.waiting() ? "WAIT" : request.rolling() ? "RUN" : backend_.triggered() ? "TD" :
                         request.trigger.mode == scope::TriggerMode::Auto ? "AUTO" : "RUN"); // Rigol's TD|WAIT|RUN|AUTO|STOP.
    } else if (match("TIMebase:SCALe") || match("TIMebase:MAIN:SCALe")) {
        if (c.query) number(request.timebaseNs / 1e9);
        else { double value; if (argument(c, value, Header::Unit::Time)) { if (value < 1e-8 || value > 20) error(-222); else { request.timebaseNs = scope::Timebases::nearest(value * 1e9); apply(request); } } }
    } else if (match("TIMebase:MAIN:OFFSet") || match("TIMebase:OFFSet")) {
        const double span=request.timebaseNs/1e8;
        if (c.query) number((.5-request.trigger.position/1000.0)*span);
        else { double value; if (argument(c,value,Header::Unit::Time)) {
            if (value<-.5*span || value>.5*span) error(-222);
            else { request.trigger.position=std::lround(500-value/span*1000); apply(request); }
        } }
    } else if (match("TRIGger:POSition")) {
        if (c.query) number(request.trigger.position/10.0);
        else { double value; if (argument(c,value)) {
            if (value<0 || value>100) error(-222);
            else { request.trigger.position=std::lround(value*10); apply(request); }
        } }
    } else if (match("TIMebase:VERNier")) {
        // Steps only (TIM:SCAL rounds to the table): the query is 0. ON/OFF are taken without effect: the
        // upstream Rigol DS driver (scopehal) sends ":TIM:VERN ON" while it connects; refusing it failed that.
        if (c.query) reply("0"); else { bool value; boolean(c, value); }
    } else if (match("ACQuire:MDEPth")) {
        if (c.query) { const auto *m = backend_.configuration(); if (m) number(m->samples); else error(-230,true); }
        else { double value; if (argument(c, value)) {
            // A depth per channel picks the smallest record budget giving at least that many points
            // to each enabled channel; a deeper request is an error, never a silent clamp.
            const uint32_t budget = value >= 1 && value <= scope::RecordDepth::Maximum && value == uint32_t(value)
                ? scope::RecordBudget::covering(uint32_t(value), request.channels()) : 0;
            if (!budget) error(-222);
            else { request.recordPoints = budget; apply(request); } } }
    } else if (match("ACQuire:ROLL")) {
        if (c.query) reply("%u",request.rollAuto);
        else { bool value; if (boolean(c,value)) { request.rollAuto=value; apply(request); } }
    } else if (match("ACQuire:PROGress")) {
        if (!c.query || *c.argument) error(-108,c.query);
        else {
            const auto progress=backend_.captureProgress(); char words[24];
            Decimal::integer(progress.words,words,sizeof words);
            reply("%u,%lu,%s,%u",unsigned(progress.active),(unsigned long)progress.sequence,words,unsigned(progress.status));
        }
    } else if (match("ACQuire:INTerleave:STATe")) { // What runs: active (0/1), the blockers.
        if (!c.query || *c.argument) error(-108, c.query);
        else { const auto s = backend_.interleave(); reply("%u,%lu", unsigned(s.active), (unsigned long)s.blockers); }
    } else if (match("ACQuire:INTerleave:MATCh")) {
        if (!c.query || *c.argument) error(-108, c.query);
        else { const auto *m = backend_.configuration(); reply("%u", unsigned(m && m->interleaveMatch)); }
    } else if (match("ACQuire:INTerleave")) {
        const auto state = backend_.interleave();
        if (c.query) {
            if (*c.argument) error(-108, true);
            else if (state.automatic) reply("AUTO");
            else reply("%u", unsigned(state.requested));
        } else if (Header::word(c.argument, "AUTO")) {
            if (!state.autoSupported) error(-241);
            else if (!backend_.interleaveAuto()) error(-221);
        }
        else { bool value; if (boolean(c, value)) {
            if (value && !state.supported) error(-241);
            else if (!backend_.interleave(value)) error(-221); // A calibration, an AutoSetup or a recording runs.
        } }
    } else if (match("ACQuire:SRATe")) {
        if (!c.query) error(-108);
        else { const auto *m = backend_.configuration(); if (m) number(m->samplesPerSecond.value()); else error(-230, true); }
    } else if (match("TRIGger:MODE")) { // The trigger type (GitHub #133): EDGE, PULSe width, RUNT, SLOPe (rise/fall time).
        static constexpr const char *Words[] = {"EDGE", "PULSe", "RUNT", "SLOPe"}, *Replies[] = {"EDGE", "PULS", "RUNT", "SLOP"};
        if (c.query) reply("%s", Replies[unsigned(request.trigger.type)]);
        else { unsigned t = 0; while (t < 4 && !Header::word(c.argument, Words[t])) ++t;
            if (t == 4) error(-224); else { request.trigger.type = scope::TriggerType(t); apply(request); } }
    } else if (match("TRIGger:POLarity")) { // The qualified trigger's polarity: the edge slope's field.
        if (c.query) reply("%s", request.trigger.edge == scope::Edge::Rising ? "POS" : "NEG");
        else { if (Header::word(c.argument, "POSitive")) request.trigger.edge = scope::Edge::Rising;
            else if (Header::word(c.argument, "NEGative")) request.trigger.edge = scope::Edge::Falling;
            else { error(-224); return true; } apply(request); }
    } else if (match("TRIGger:WHEN")) {
        static constexpr const char *Words[] = {"ANY", "LESS", "GREater", "INSide", "OUTSide"}, *Replies[] = {"ANY", "LESS", "GRE", "INS", "OUTS"};
        if (c.query) reply("%s", Replies[unsigned(request.trigger.condition)]);
        else { unsigned w = 0; while (w < 5 && !Header::word(c.argument, Words[w])) ++w;
            if (w == 5) error(-224); else { request.trigger.condition = scope::TriggerCondition(w); apply(request); } }
    } else if (match("TRIGger:TIME:LOWer") || match("TRIGger:TIME:UPPer")) {
        uint32_t &limit = match("TRIGger:TIME:LOWer") ? request.trigger.minimumNs : request.trigger.maximumNs;
        if (c.query) number(limit / 1e9);
        else { double value; if (argument(c, value, Header::Unit::Time)) {
            const double ns = std::round(value * 1e9);
            if (!(ns >= scope::TriggerSettings::MinimumNs && ns <= scope::TriggerSettings::MaximumNs)) error(-222);
            else { limit = uint32_t(ns); apply(request); } } }
    } else if (match("TRIGger:LEVel:HIGH") || match("TRIGger:LEVel:LOW")) { // The band of RUNT and SLOPe.
        const bool high = match("TRIGger:LEVel:HIGH");
        const bool primaryHigh = request.trigger.level >= request.trigger.level2;
        const bool primary = high == primaryHigh; // `level` is the band's end named: the keys' level.
        int16_t &field = primary ? request.trigger.level : request.trigger.level2;
        const int other = primary ? request.trigger.level2 : request.trigger.level;
        const auto *metadata = backend_.configuration();
        if (!metadata || !metadata->channels[unsigned(request.trigger.channel)].calibrated) error(-230, c.query);
        else { const auto &ch = metadata->channels[unsigned(request.trigger.channel)];
            const double scale = ch.voltsPerCode * ch.probeNumerator / ch.probeDenominator;
            if (c.query) number((field - ch.zeroCode) * scale);
            else { double value; if (argument(c, value, Header::Unit::Voltage)) {
                const double code = value / scale + ch.zeroCode;
                if (!(code >= -128 && code <= 127)) error(-222);
                else {
                    const auto rounded = int16_t(std::lround(code));
                    if (high ? rounded < other : rounded > other) error(-222);
                    else { field = rounded; apply(request, primary); }
                }
            } } }
    } else if (match("TRIGger:SWEep") || match("TRIGger:EDGE:SWEep")) {
        if (c.query) reply("%s", request.trigger.mode == scope::TriggerMode::Auto ? "AUTO" : request.trigger.mode == scope::TriggerMode::Normal ? "NORM" : "SING");
        else { if (Header::word(c.argument, "AUTO")) request.trigger.mode = scope::TriggerMode::Auto;
            else if (Header::word(c.argument, "NORMal")) request.trigger.mode = scope::TriggerMode::Normal;
            else if (Header::word(c.argument, "SINGle")) request.trigger.mode = scope::TriggerMode::Single;
            else { error(-224); return true; } apply(request); }
    } else if (match("TRIGger:EDGE:SOURce")) {
        if (c.query) reply("CHAN%u", unsigned(request.trigger.channel) + 1);
        else { unsigned ch; if (!Header::matches(c.argument, "CHANnel#", &ch) || ch < 1 || ch > 2) error(-224);
            else if (!request.frontend.enabled[ch - 1]) error(-221); // A source that is not captured.
            else { request.trigger.channel = scope::Channel(ch - 1); apply(request); } }
    } else if (match("TRIGger:EDGE:SLOPe")) {
        if (c.query) reply("%s", request.trigger.edge == scope::Edge::Rising ? "POS" : "NEG");
        else { if (Header::word(c.argument, "POSitive")) request.trigger.edge = scope::Edge::Rising;
            else if (Header::word(c.argument, "NEGative")) request.trigger.edge = scope::Edge::Falling;
            else { error(-224); return true; } apply(request); }
    } else if (match("TRIGger:EDGE:NREJect")) { // Noise reject: hysteresis of the edge's arming.
        if (c.query) reply("%u", unsigned(request.trigger.reject));
        else { bool value; if (boolean(c, value)) { request.trigger.reject = value; apply(request); } }
    } else if (match("TRIGger:EDGE:LEVel")) {
        const auto *metadata = backend_.configuration();
        if (!metadata || !metadata->channels[unsigned(request.trigger.channel)].calibrated) error(-230, c.query);
        else { const auto &ch = metadata->channels[unsigned(request.trigger.channel)];
            const double scale = ch.voltsPerCode * ch.probeNumerator / ch.probeDenominator;
            if (c.query) number((request.trigger.level - ch.zeroCode) * scale);
            else { double value; if (argument(c, value, Header::Unit::Voltage)) { const double code = value / scale + ch.zeroCode;
                if (code < -128 || code > 127) error(-222); else { request.trigger.level = std::lround(code); apply(request, true); } } } }
    } else return false;
    return true;
}
} // namespace openosc::scpi
