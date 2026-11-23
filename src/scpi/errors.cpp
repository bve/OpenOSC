#include "openosc/scpi/errors.hpp"

namespace openosc::scpi {
void ErrorQueue::push(int16_t code) {
    if (!code) return;
    event_ |= code <= -100 && code > -200 ? 32 : code <= -200 && code > -300 ? 16 : code <= -300 && code > -400 ? 8 : 4;
    if (count_ == 8) { errors_[(head_ + 7) % 8] = -350; event_ |= 8; return; }
    errors_[(head_ + count_) % 8] = code; ++count_;
}
int16_t ErrorQueue::pop() {
    if (!count_) return 0;
    const auto code = errors_[head_]; head_ = (head_ + 1) % 8; --count_; return code;
}
const char *ErrorQueue::message(int16_t code) {
    switch (code) {
    case 0: return "No error";
    case -102: return "Syntax error";
    case -104: return "Data type error";
    case -108: return "Parameter not allowed";
    case -109: return "Missing parameter";
    case -113: return "Undefined header";
    case -114: return "Header suffix out of range";
    case -131: return "Invalid suffix";
    case -200: return "Execution error";
    case -221: return "Settings conflict";
    case -222: return "Data out of range";
    case -224: return "Illegal parameter value";
    case -230: return "Data unavailable or invalid";
    case -240: return "Hardware error";
    case -241: return "Hardware missing";
    case -250: return "Mass storage error";
    case -256: return "File name not found";
    case -257: return "File name error";
    case -350: return "Queue overflow";
    case -363: return "Input buffer overrun";
    case -410: return "Query interrupted";
    case -420: return "Query unterminated";
    default: return "Instrument error";
    }
}
} // namespace openosc::scpi
