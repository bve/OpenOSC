#include "openosc/instrument/data_access.hpp"
#include "openosc/acquisition/sample_plan.hpp"

namespace openosc {
bool DataAccess::resumeScope(const ScopeRequest &request) {
    revokeBackground(); // Revoked first: nothing waits for a background reader.
    if (zeroBusy()) return false;
    if (!scopeHeld_) return configureScopeCore(request);
    // Keep the frozen record until the new acquisition has accepted its settings: with two slots
    // the new capture writes the other one. A capture that needs the held memory (one slot: the
    // compact geometry, the 512 KiB budget, or Roll) may claim it only for its own start: the
    // record leaves the store but its bytes stay until the first DMA transfer, and every refusal
    // comes before that, so a refused
    // RUN puts the same record back into HOLD.
    if (recorder_.busy() || recordPending_ || store_.busy() || !validateScope(request) ||
        store_.readers() != (heldFrame_.valid() ? 1U : 0U)) return false;
    const auto previous = request_;
    request_ = request;
    scope::SamplePlan plan;
    const bool planned = samplePlan(plan);
    const auto geometry = recordGeometry(); // Slots differ between the geometries.
    request_ = previous;
    if (!planned) return false;

    const auto configuration = configuration_;
    const auto trigger = heldTrigger_;
    const auto configuredAt = configuredAt_, armSequence = armSequence_, triggerFloor = triggerFloor_;
    const bool singleDone = singleDone_, failed = captureFailed_, owed = captureRestartOwed_, waiting = triggerWaiting_, staged = staged_;
    const auto reject = [&] {
        const auto failure = status_;
        stopCapture(false); // configureScopeCore may have restarted the previous acquisition.
        request_ = previous;
        configuration_ = configuration;
        configuredAt_ = configuredAt; armSequence_ = armSequence; triggerFloor_ = triggerFloor;
        singleDone_ = singleDone; captureFailed_ = failed; captureRestartOwed_ = owed; triggerWaiting_ = waiting; staged_ = staged;
        heldTrigger_ = trigger;
        scopeHeld_ = true;
        if (!heldFrame_.valid()) store_.acquireLatest(heldFrame_);
        status_ = failure;
        return false;
    };
    unsigned slot=0;
    const bool sharesMemory = request.rolling() || !arena_.freeSlot(request.recordPoints, slot, geometry);
    scope::FrameView held;
    uint8_t heldRegion = 0;
    if (sharesMemory) {
        heldFrame_.release();
        if (store_.hasFrame() && !store_.detach(held, heldRegion)) { status_ = DataStatus::Busy; return reject(); }
        if (heldRegion) arena_.release(heldRegion); // For the new capture's claim only; nothing writes it before its DMA.
    }
    const auto putBack = [&] {
        if (!heldRegion) return;
        // Taken back only from a capture proven stopped: one that would not stop keeps its memory.
        if (!stopCapture(false) || !arena_.claim(heldRegion)) return;
        if (!store_.adopt(held, heldRegion)) arena_.release(heldRegion); // Only the region claimed here.
    };
    scopeHeld_ = false; singleDone_ = false; captureFailed_ = false; holdPending_ = false;
    if (request.trigger.mode == scope::TriggerMode::Single) armSequence_ = sequence_ + 1;
    // The trigger event floor before any start: configureScopeCore may start the first capture of
    // this RUN at once (++sequence_), and a floor taken after it would drop that record's trigger.
    // A change takes the same floor again before its start; same() starts nothing here.
    restartTriggerEvents();
    // Settings staged in HOLD are already request_: same() would keep a capture still running the old
    // plan (holdPending_) or skip the restart, so a staged request is applied as a change.
    if (!configureScopeCore(request, failed || owed || staged, true)) { putBack(); return reject(); }

    heldFrame_.release();
    if (!store_.cancel()) { status_ = DataStatus::Busy; return reject(); }
    heldTrigger_.forget();
    ++holdReleases_;
    triggerWaiting_ = request.trigger.mode != scope::TriggerMode::Auto; acceptedTriggered_ = false;
    return true;
}
} // namespace openosc
