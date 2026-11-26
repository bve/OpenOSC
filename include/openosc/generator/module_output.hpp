#pragma once
#include "openosc/generator/module_link.hpp"
#include "openosc/generator/module_recovery.hpp"
#include "openosc/generator/arbitrary.hpp"
#include "openosc/platform/resources.hpp"

namespace openosc::generator {
// ZT706S: the external generator module on USART3, PC10 TX / PC11 RX (AF7, pull-up), 9600 8N1, IRQ39,
// as the factory main and GEN (706S main 0802C278, GEN 2406840C). A setting is one 512-byte frame
// (ModuleWave) the TXE interrupt sends in ~0.53 s; nothing waits for it in the main loop. No DMA
// (DMA1 Stream0 is the Scope capture's) and no DAC: it neither loads the bus nor lowers the
// capture's maximum rate. The kernel clock is PCLK1, the mux UART5 (DMM) shares.
// Run-mode changes only between frames: clockBusy() while one is on the wire (the change is refused
// as Busy, nothing stopped), then the UART stops and starts again on the new PCLK1; a frame that was
// waiting for its acknowledge goes again whole. The output is not switched off for it.
// enabled() is what was asked (ON/OFF); applied what the module acknowledged (unknown after reset, a
// timeout, a stuck UART or while a frame is on its way): never On without the module's acknowledge.
// status(): Busy until acknowledged, Ok, Timeout (no acknowledge after ModuleLink::Attempts),
// HardwareError (UART did not start or finish). After a failure the latest requested state goes
// again (ModuleRecovery: bounded rounds, a stuck UART started anew); ON or OFF asked again after a
// failure is a new request, even with the same settings.
// The Arbitrary waveform (docs/AWG.md): its codes as loaded, in two banks of SRAM4 (.generator, unused
// on this board without the DAC): one holds the loaded waveform, a new one is staged in the other by one
// owner at a time (ArbitraryBanks) and committed only whole and validated. Played as the module's own
// frame (ModuleWave::buildArbitrary): ON, a new waveform or rate while ON, recovery; OFF is the zero
// frame as for every shape. RAM only: after a reset nothing is loaded and an Arbitrary output stays off.
class ModuleOutput {
public:
    enum class Applied : uint8_t { Unknown, Off, On };
    explicit ModuleOutput(PeripheralResources &);
    ~ModuleOutput() { stop(); }
    bool configure(const Settings &settings);   // Refuses what the module cannot do (Capabilities).
    bool enable();                              // Again after a failure: a new request.
    // OFF whenever the module may not be off: ON asked, or not acknowledged as off (the boot, after
    // a timeout or a stuck UART) - so a failed OFF is tried again; not again while one is queued.
    void disable();
    // The ZT703S calibration reference (Output::reference) has no counterpart: the ZT706S jack is the
    // module's, and OFF stays the module's zero waveform (its factory MSP gives PA6 no AF either).
    void reference(bool) {}
    void service();
    bool clockBusy() const { return !link_.suspendable(); }
    bool suspendForClock();
    bool retime(bool enable);
    bool enabled() const { return requested_; }
    bool busy() const { return link_.busy(); }
    bool loadsBus() const { return false; }
    Applied applied() const { return applied_; }
    unsigned recoveryRounds() const { return recovery_.rounds(); }
    bool recovering() const { return recovery_.waiting(); } // A failed exchange's next round is due (ModuleRecovery).
    bool offConfirmed() const { return !busy() && applied_ == Applied::Off; }
    bool onConfirmed() const { return !busy() && applied_ == Applied::On; } // Acknowledged by the module.
    DataStatus status() const { return portFailed_ ? DataStatus::HardwareError : link_.status(); }
    const Settings &settings() const { return settings_; }
    const OutputPlan &plan() const { return plan_; }
    // No DAC: no clock limit, no stream faults.
    Snapshot snapshot() const {
        return {settings_, plan_, status(), requested_, 0, 0, OutputFault::None, banks_.points(), banks_.source(), banks_.revision(), banks_.samples()};
    }
    // As Output's: a stage in the bank that holds no loaded waveform (never Conflict: nothing else
    // uses the banks); commitArbitrary makes it the loaded one and, while Arbitrary is ON, sends its
    // frame (Busy until the module's acknowledge, as any ON); a UART that does not start brings the
    // previous waveform back (Refused).
    ArbitraryBanks::Stage beginArbitrary(const void *owner) {
        return banks_.begin(owner, banks_.loaded() ? banks_.bank() : 0, ArbitraryBanks::None);
    }
    bool putArbitrary(const void *owner, unsigned index, uint16_t code) { return banks_.put(owner, index, code); }
    enum class Commit : uint8_t { Done, Lost, Refused };
    Commit commitArbitrary(const void *owner, unsigned points, ArbitrarySource source);
    void abortArbitrary(const void *owner) { banks_.abort(owner); }
    bool holdsArbitrary(const void *owner) const { return banks_.holds(owner); }
    const uint16_t *arbitrarySamples() const { return banks_.samples(); } // points() codes; null: none.

private:
    bool start();
    void stop();
    bool request(bool on);                      // Submitted: a fresh recovery budget; refused: nothing changes.
    bool submit(bool on);
    // The frame `settings` play (ON) or OFF; Arbitrary from the loaded waveform (none: false).
    bool frame(const Settings &settings, bool on, ModuleWave::Frame &frame) const;
    void planned(const ModuleWave::Frame &frame) { plan_ = {}; plan_.count = uint16_t(frame.timing.count); plan_.actualHz = frame.actualHz; }
    void failed(uint32_t now);
    ArbitraryBanks banks_;
    ModuleLink link_;
    ModuleRecovery recovery_;
    Settings settings_;
    OutputPlan plan_;
    Applied applied_ = Applied::Unknown;
    uint32_t sentEpoch_ = 0;            // The interrupt's TC count when the frame awaiting its ACK ended.
    bool requested_ = false, running_ = false, portFailed_ = false, onWire_ = false;
};
} // namespace openosc::generator
