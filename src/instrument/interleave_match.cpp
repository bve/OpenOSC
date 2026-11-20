#include "openosc/instrument/data_access.hpp"
#if OPENOSC_BOARD == 703
#include "openosc/calibration/interleave_match_wizard.hpp"
#include "openosc/platform/interrupts.hpp"
#include "stm32h7xx.h"

namespace openosc {
DataStatus DataAccess::interleaveMatchReady() const {
    if (interleaveMeasure_.active() || zeroBusy() || zeroBlocked_ || internalCapture() || recorder_.busy() ||
        scopeHeld_ || holdPending_ || !active_ || stopping_ || measurement_ != MeasurementMode::Oscilloscope ||
        generator_.enabled()) return DataStatus::Busy;
    // A real factory record: the profile is bound to its CRC (the defaults' scale is no identity).
    if (!calibration_.valid() || !interleaveCalibrated() || !configuration_.channels[0].calibrated) return DataStatus::Uncalibrated;
    if (!interleaving(request_) || configuration_.layout != scope::Layout::InterleavedCh1 ||
        request_.rolling() || request_.trigger.mode != scope::TriggerMode::Auto ||
        configuration_.samplesPerSecond.value() != scope::SelectableInterleave::rate(TimerClock::apb1()) ||
        configuration_.samples < calibration::InterleaveMatchWizard::Points) return DataStatus::InvalidArgument;
    return interleaveHeadroom_ ? DataStatus::Overload : DataStatus::Ok;
}
DataStatus DataAccess::saveInterleaveMatch(uint32_t coreHz, const Rational &rate, const scope::ChannelMetadata &channel,
                                           const scope::InterleaveMatch &filter) {
    if (!matchCalibrating_ || interleaveMatchReady() != DataStatus::Ok) return DataStatus::Busy;
    return calibration::InterleaveMatchProfiles::save(coreHz, rate, channel, filter,
                                                      reinterpret_cast<const uint32_t *>(UID_BASE));
}
} // namespace openosc
#endif
