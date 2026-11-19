#include "openosc/instrument/data_access.hpp"
#include "openosc/board/current.hpp"

namespace openosc {
bool DataAccess::calibrate(uint32_t now,calibration::ZeroProcedure &procedure) {
    revokeBackground(); // Revoked first: nothing waits for a background reader.
    if (!zeroOperation_.begin(now,procedure)) return false;
    counters_.stop();
    return true;
}
bool DataAccess::calibrateZero(uint32_t now,bool hardware) {
    return calibrate(now,hardware ? static_cast<calibration::ZeroProcedure &>(hardwareZero_) : softwareZero_);
}
bool DataAccess::gainSpan(unsigned channel,unsigned range,double referenceVolts,double &codes) const {
    return channel<2 && calibration::GainCalibration::expectedSpan(calibration_,channel,range,referenceVolts,
               request_.probeNumerator[channel],request_.probeDenominator[channel],codes);
}
DataStatus DataAccess::calibrateGain(uint32_t now,unsigned channel,unsigned range,double referenceVolts) {
    double codes=0;
    if (zeroBusy()) return DataStatus::Busy;
    if (!calibration_.valid()) return DataStatus::Uncalibrated;
    // The reference against the channel's probe, before anything changes.
    if (!gainSpan(channel,range,referenceVolts,codes) || !calibration::GainCalibration::spanAllowed(codes)) return DataStatus::InvalidArgument;
    gain_.configure(channel,range,referenceVolts);
    return calibrate(now,gain_) ? DataStatus::Ok : DataStatus::Busy;
}
bool DataAccess::factoryGain() {
    if (zeroBusy() || !calibration_.valid() || !gainFile_.loaded()) return false;
    if (!gainFile_.values().enabled) return true;
    auto values=gainFile_.values(); values.enabled=false;
    if (!gainFile_.save(values)) return false; // The gain in force stays.
    scopeCalibration_.setGain(values);
    calibrationChanged();
    return true;
}
DataStatus DataAccess::interleaveChange() const {
    if (!board::Current::Features.adcInterleave) return DataStatus::Unsupported; // ZT703S: no interleaved capture.
    if (!calibration_.valid()) return DataStatus::Uncalibrated;
    return zeroBusy() ? DataStatus::Busy : DataStatus::Ok;
}
DataStatus DataAccess::applyInterleave(float codes) {
    const auto status=interleaveChange();
    if (status!=DataStatus::Ok) return status;
    calibration::InterleaveProfile profile; profile.factoryCrc=calibration_.crc(); profile.enabled=true; profile.offset=codes;
    if (!scopeCalibration_.setInterleave(profile)) return DataStatus::InvalidArgument;
    calibrationChanged();
    return DataStatus::Ok;
}
DataStatus DataAccess::saveInterleave() {
    const auto status=interleaveChange();
    if (status!=DataStatus::Ok) return status;
    return interleaveFile_.save(scopeCalibration_.interleave()) ? DataStatus::Ok : DataStatus::IoError;
}
DataStatus DataAccess::cancelInterleave() {
    const auto status=interleaveChange();
    if (status!=DataStatus::Ok) return status;
    if (!interleaveFile_.loaded() || !scopeCalibration_.setInterleave(interleaveFile_.values())) return DataStatus::IoError;
    calibrationChanged();
    return DataStatus::Ok;
}
DataStatus DataAccess::resetInterleave() {
    const auto status=interleaveChange();
    if (status!=DataStatus::Ok) return status;
    calibration::InterleaveProfile factory; factory.factoryCrc=calibration_.crc();
    if (!interleaveFile_.save(factory)) return DataStatus::IoError; // The one in force stays.
    scopeCalibration_.setInterleave(factory);
    calibrationChanged();
    return DataStatus::Ok;
}
void DataAccess::cancelZero(uint32_t now) {
    const bool wasBusy=zeroBusy();
    zeroOperation_.cancel(now);
    if (wasBusy && !zeroBusy()) { zeroBlocked_=false; counters_.start(); }
}
DataStatus DataAccess::zeroAvailable() const {
    if (!calibration_.valid()) return DataStatus::Uncalibrated;
    if (!active_ || stopping_ || measurement_!=MeasurementMode::Oscilloscope) return DataStatus::Unsupported;
    return scopeRecording() ? DataStatus::Busy : DataStatus::Ok;
}
bool DataAccess::zeroStopGenerator() {
    generator_.disable();
    return !generator_.enabled();
}
void DataAccess::zeroRestoreFailed(const ScopeRequest &original) {
    stopCapture(false); heldFrame_.release(); store_.cancel();
    request_=original; configuration_={};
    scopeHeld_=true; captureFailed_=true; status_=DataStatus::HardwareError;
    zeroBlocked_=true; zeroMinimumSequence_=sequence_+1;
}
void DataAccess::serviceZero(uint32_t now) {
    const bool wasBusy=zeroBusy();
    zeroOperation_.service(now);
    if (wasBusy && !zeroBusy() && zeroOperation_.result()!=calibration::ZeroOperation::Result::RestoreFailed) {
        zeroBlocked_=false; counters_.start();
    }
    if (zeroBlocked_ && !zeroBusy() && store_.ready()) {
        scope::FrameLease frame;
        if (store_.acquireLatest(frame) && int32_t(frame.view().metadata().sequence-zeroMinimumSequence_)>=0) {
            zeroBlocked_=false; counters_.start();
        }
    }
}
bool DataAccess::saveInterleaveProfile(const calibration::InterleaveProfile &profile) { return interleaveFile_.save(profile); }
void DataAccess::calibrationChanged() {
    if (active_ && !stopping_ && measurement_==MeasurementMode::Oscilloscope && !scopeHeld_) configureScopeCore(request_,true);
}
bool DataAccess::reloadCalibration() {
    if (zeroBusy() || !calibration_.valid()) return false;
    const bool pwm=pwmFile_.load() && scopeCalibration_.setPwm(pwmFile_.values());
    const bool software=zeroFile_.load(calibration_.crc());
    const bool gain=gainFile_.load() && scopeCalibration_.setGain(gainFile_.values());
    const bool interleave=interleaveFile_.load() && scopeCalibration_.setInterleave(interleaveFile_.values());
    calibrationChanged();
    return pwm && software && gain && interleave;
}
calibration::ZeroSelection::Result DataAccess::factoryZero() {
    using Result=calibration::ZeroSelection::Result;
    if (zeroBusy() || !calibration_.valid()) return Result::Unavailable;
    const auto result=calibration::ZeroSelection::useFactory(zeroFile_,pwmFile_,scopeCalibration_);
    calibrationChanged();
    return result;
}
} // namespace openosc
