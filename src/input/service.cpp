#include "openosc/platform/input_service.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/critical_section.hpp"

namespace openosc {
namespace {
KeyInput input; volatile bool enabled=false;
// The injected key (InputService::inject): set under a CriticalSection, released when its time is up by
// the SysTick tick, the only caller that is not in a CriticalSection.
volatile uint32_t injectedKeys=0, injectedUntil=0;
volatile bool injectedOwed=false; // The injection's events are not all taken yet; written by the foreground only.
void release(uint32_t now) { if (injectedKeys && int32_t(now-injectedUntil)>=0) injectedKeys=0; }
uint32_t sampled(uint32_t now) { release(now); return Keypad::read()|injectedKeys; }
}
void InputService::enable() {
    CriticalSection guard;
    input.reset(sampled(Board::milliseconds()),Board::milliseconds()); enabled=true;
}
void InputService::tick(uint32_t now) { if (enabled) input.sample(sampled(now),now); else release(now); }
uint32_t InputService::keys() { return input.keys(); }
bool InputService::activity() { return input.activity(); }
bool InputService::take(TimedKeyEvents &event) { return input.take(event); }
bool InputService::overflowed() { return input.dropped()!=0; }
void InputService::suppress() {
    CriticalSection guard;
    input.reset(sampled(Board::milliseconds()),Board::milliseconds());
}
void InputService::wake() {
    CriticalSection guard;
    input.wake(sampled(Board::milliseconds()),Board::milliseconds());
}
bool InputService::powerHold(uint32_t &elapsed) { return input.powerHold(elapsed); }
bool InputService::inject(uint32_t key, uint32_t ms) {
    CriticalSection guard;
    if (injecting()) return false;
    injectedUntil=Board::milliseconds()+ms; injectedKeys=key; injectedOwed=true;
    return true;
}
// Held, or released but its events not yet through the debounce and taken by the foreground: *OPC? then
// means the key's gestures have been handled.
bool InputService::injecting() {
    if (injectedKeys) return true; // The SysTick tick releases it (board.cpp: HAL_IncTick, then tick).
    // Unsigned: past its release the difference grows, so a flag left set turns busy again only for the
    // debounce once per 49.7-day wrap of the millisecond counter, not for half of it.
    if (injectedOwed && (uint32_t(Board::milliseconds()-injectedUntil)<KeyGestures::DebounceMs || input.pending())) return true;
    injectedOwed=false;
    return false;
}
} // namespace openosc
