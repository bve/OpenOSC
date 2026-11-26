#pragma once
#include "openosc/acquisition/counter_window.hpp"
#include "openosc/acquisition/reciprocal_counter.hpp"
#include "openosc/platform/resources.hpp"

namespace openosc::scope {
// The comparators' hardware counters: CH1 on TIM2 (PA15, AF1: TIM2_ETR and TIM2_CH1), CH2 on TIM4
// (PE0, ETR), windows on TIM5's microseconds. With timed(true) a slow CH1 is timed instead
// (ReciprocalPolicy): TIM2 leaves its ETR count for input capture on TI1 and its interrupt logs the
// edges (EdgeLog). CH2's count and windows go on unchanged. With timed(false), the default, TIM2
// never leaves its ETR count and has no interrupt.
class PulseCounters {
public:
    // Below the capture DMA (5), the trigger (3) and USB (4); at most FastLimit fast edges a window.
    static constexpr unsigned CapturePriority=6;
    explicit PulseCounters(PeripheralResources &resources):resources_(resources) {}
    ~PulseCounters() { stop(); }
    bool start();
    void stop();
    void invalidate(uint32_t now);
    void service(uint32_t now);
    void timed(bool on); // Timed edges for CH1 below 5 kHz; kept across stop() and start().
    bool timing() const { return policy_.timing(); }
    const CounterSnapshot &snapshot() const { return snapshot_; }
private:
    uint32_t read(uint32_t (&counts)[2]) const;
    void filter(uint8_t level) const; // The ETR filter (CounterLadder::Filters) of each counting channel.
    void cycle(uint32_t now);         // A completed gate cycle.
    void enter(const CounterReading &gate,uint32_t now);
    void serviceTimed(uint32_t now);
    void leave(ReciprocalPolicy::Event why,uint32_t now);
    void publish(unsigned channels);
    uint32_t kernelHz_=0; // f_CK_INT of TIM2/TIM4.
    static void interrupt();
    static void capture();
    static PulseCounters *active_;
    PeripheralResources &resources_;
    PeripheralLease lease_;
    CounterWindow window_;
    ReciprocalPolicy policy_;
    EdgeLog log_; // Written by capture() while timing.
    CounterSnapshot snapshot_;
    uint32_t polledAt_=0;
    volatile uint32_t high_=0;
    bool timed_=false;
};
} // namespace openosc::scope
