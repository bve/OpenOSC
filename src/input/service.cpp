#include "openosc/platform/input_service.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/critical_section.hpp"

namespace openosc {
namespace { KeyInput input; volatile bool enabled=false; }
void InputService::enable() {
    CriticalSection guard;
    input.reset(Keypad::read(),Board::milliseconds()); enabled=true;
}
void InputService::tick(uint32_t now) { if (enabled) input.sample(Keypad::read(),now); }
uint32_t InputService::keys() { return input.keys(); }
bool InputService::activity() { return input.activity(); }
bool InputService::take(TimedKeyEvents &event) { return input.take(event); }
bool InputService::overflowed() { return input.dropped()!=0; }
void InputService::suppress() {
    CriticalSection guard;
    input.reset(Keypad::read(),Board::milliseconds());
}
void InputService::wake(bool relativeHold) {
    CriticalSection guard;
    input.wake(Keypad::read(),Board::milliseconds(),relativeHold);
}
bool InputService::powerHold(uint32_t &elapsed) { return input.powerHold(elapsed); }
} // namespace openosc
