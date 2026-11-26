#include "openosc/instrument/data_access.hpp"

namespace openosc {
// AutoSetup keeps the frame on show in its arena region and measures in the trial area.
bool DataAccess::retainScope() {
    revokeBackground(); // Revoked first: nothing waits for a background reader.
    if (retainedRegion_ || zeroBusy()) return !zeroBusy();
    const bool held = scopeHeld_;
    if (holdPending_) { holdPending_ = false; stopCapture(false); }
    heldFrame_.release();
    if (!store_.hasFrame()) return true; // Nothing on show to come back to.
    if (store_.readers() || !store_.detach(retained_, retainedRegion_)) {
        if (held) store_.acquireLatest(heldFrame_);
        return false; // A USB transfer or a file export is reading it.
    }
    return true;
}
void DataAccess::releaseRetained() {
    trials_ = false;
    if (retainedRegion_) arena_.release(retainedRegion_);
    retainedRegion_ = 0; retained_ = {};
}
// Back to HOLD with the settings and the record from before AutoSetup, untouched in place.
bool DataAccess::restoreHeldScope(const ScopeRequest &request) {
    return !zeroBusy() && restoreHeld(request);
}
bool DataAccess::restoreHeld(const ScopeRequest &request) {
    if (!validateScope(request) || store_.readers()) return false;
    holdScopeCore(false);
    trials_ = false;
    if (!stopCapture(false) || !store_.drop()) return false;
    request_ = request;
    if (!describeNext(configuration_)) describeChannels(configuration_); // The plan of the restored settings.
    // The analog front end returns to the held record's settings, as at a HOLD activation.
    if (active_ && !frontend_.scope(appliedFrontend(request_), request_.trigger.comparatorLevel())) status_ = DataStatus::Uncalibrated;
    // A running trend AutoSetup interrupted (or a STOP's record not on show yet): the STOP below ends its
    // record, which replaces the frame retained.
    trend_.stop();
    const bool trend = trend_.owed();
    if (trend) releaseRetained();
    holdScopeCore(true);
    if (trend) { status_ = DataStatus::Ok; return true; }
    if (!retainedRegion_) return true; // The original HOLD had no completed frame.
    const auto region = retainedRegion_;
    retainedRegion_ = 0;
    if (!store_.adopt(retained_, region)) { arena_.release(region); retained_ = {}; return false; }
    retained_ = {};
    if (!store_.acquireLatest(heldFrame_)) return false;
    triggerWaiting_ = false; status_ = DataStatus::Ok;
    return true;
}
} // namespace openosc
