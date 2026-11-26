#include "openosc/settings/instrument_state.hpp"
#include "openosc/generator/frequency.hpp"

namespace openosc {
ScopeRequest InstrumentState::scope(const Preferences &p) {
    ScopeRequest request;
    request.timebaseNs = p.timebaseNs;
    request.recordPoints = p.scope.recordPoints;
    request.rollAuto=p.scope.rollAuto;
    request.longerRecord = p.scope.longerRecord;
    request.trigger = {scope::Channel(p.scope.triggerSource), scope::Edge(p.scope.triggerEdge),
                       scope::TriggerMode(p.scope.triggerMode), int16_t(p.scope.triggerLevel)};
    request.trigger.position=p.scope.triggerPosition;
    request.trigger.reject=p.scope.triggerReject!=0;
    request.trigger.type = scope::TriggerType(p.scope.triggerType);
    request.trigger.condition = scope::TriggerCondition(p.scope.triggerCondition);
    request.trigger.level2 = int16_t(p.scope.triggerLevel2);
    request.trigger.minimumNs = p.scope.triggerMinimumNs; request.trigger.maximumNs = p.scope.triggerMaximumNs;
    for (unsigned c = 0; c < 2; ++c) {
        scope::AnalogFrontend::rangeForMillivolts(p.millivolts[c], request.frontend.range[c]);
        request.frontend.enabled[c] = p.scope.enabled[c];
        request.frontend.coupling[c] = scope::Coupling(p.scope.coupling[c]);
        request.frontend.position[c] = p.scope.position[c];
        request.probeNumerator[c] = p.scope.probeNumerator[c];
        request.probeDenominator[c] = p.scope.probeDenominator[c];
    }
    request.followChannels(); // A file may name a trigger source that is switched off.
    return request;
}
void InstrumentState::capture(const ScopeRequest &r, Preferences &p) {
    p.timebaseNs = r.timebaseNs; p.scope.recordPoints = r.recordPoints;
    p.scope.rollAuto=r.rollAuto;
    p.scope.longerRecord = r.longerRecord;
    p.scope.triggerSource = unsigned(r.trigger.channel); p.scope.triggerEdge = unsigned(r.trigger.edge);
    p.scope.triggerMode = unsigned(r.trigger.mode); p.scope.triggerLevel = r.trigger.level;
    p.scope.triggerPosition=r.trigger.position;
    p.scope.triggerReject=r.trigger.reject;
    p.scope.triggerType = unsigned(r.trigger.type); p.scope.triggerCondition = unsigned(r.trigger.condition);
    p.scope.triggerLevel2 = r.trigger.level2;
    p.scope.triggerMinimumNs = r.trigger.minimumNs; p.scope.triggerMaximumNs = r.trigger.maximumNs;
    for (unsigned c = 0; c < 2; ++c) {
        p.millivolts[c] = scope::AnalogFrontend::millivoltsForRange(r.frontend.range[c]);
        p.scope.enabled[c] = r.frontend.enabled[c]; p.scope.coupling[c] = unsigned(r.frontend.coupling[c]);
        p.scope.position[c] = r.frontend.position[c]; p.scope.probeNumerator[c] = r.probeNumerator[c];
        p.scope.probeDenominator[c] = r.probeDenominator[c];
    }
}
void InstrumentState::capture(const generator::Settings &actual, Preferences &prefs_) {
    prefs_.generatorShape = unsigned(actual.shape);
    prefs_.generatorLevel = unsigned(actual.level);
    prefs_.generatorSquareDrive = unsigned(actual.squareDrive);
    prefs_.generatorMinimumCode = actual.minimumCode;
    prefs_.generatorMaximumCode = actual.maximumCode;
    prefs_.generatorMillihertz = generator::Frequency::millihertz(actual.frequencyHz);
    prefs_.generatorDutyPermille = actual.dutyPermille;
    prefs_.generatorAmplitudeMillivolts = actual.amplitudeMillivolts;
    prefs_.generatorArbitraryRate = actual.arbitrarySamplesPerSecond;
}
generator::Settings InstrumentState::generator(const Preferences &p) {
    generator::Settings result;
    result.shape = generator::Shape(p.generatorShape);
    result.level = generator::Level(p.generatorLevel);
    result.squareDrive = generator::SquareDrive(p.generatorSquareDrive);
    result.minimumCode = p.generatorMinimumCode;
    result.maximumCode = p.generatorMaximumCode;
    result.frequencyHz = generator::Frequency::rational(p.generatorMillihertz);
    result.dutyPermille = p.generatorDutyPermille;
    result.amplitudeMillivolts = uint16_t(p.generatorAmplitudeMillivolts);
    result.arbitrarySamplesPerSecond = p.generatorArbitraryRate;
    return result;
}
} // namespace openosc
