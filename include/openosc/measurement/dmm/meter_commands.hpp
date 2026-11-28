#pragma once
#include "openosc/measurement/dmm/function.hpp"
#include "openosc/measurement/dmm/key_command.hpp"
#include "openosc/measurement/dmm/range_stroke.hpp"
#include "openosc/measurement/dmm/ranges.hpp"

namespace openosc::dmm {
// The controller's command UART as the commands see it (UartDma; a scripted port in tests).
class CommandPort {
public:
    virtual bool send(CommandGroup group, uint32_t nowMs) = 0; // Accepted: txStatus Busy until it left.
    virtual DataStatus txStatus() const = 0;
protected:
    ~CommandPort() = default;
};
// Every DMM command on the UART, one operation at a time: the function selector (F keys, SCPI, the mA
// restore), the controller keys (KeyCommand: AUTO/RANGE, HOLD release, REL off) and the range selector,
// AUTO/RANGE always as one RangeStroke. While a stroke runs nothing else is sent and nothing new is
// accepted; the next command goes only once the stroke's end has left the UART. Requests refused here
// change nothing. `minimumUs`: the DMM's activation (a packet older than it is not this session's).
class MeterCommands {
public:
    // A physical press needs a reading of this session no older than this (a blind 20 is never sent).
    static constexpr uint64_t FreshUs = 2000000;
    bool selectFunction(Function function, const Result &latest, uint32_t nowMs, uint64_t minimumUs);
    // A physical F1-F4 as the factory's (706S F1 0x08043C5C, 703S 0x080432D8: 50 50 at the release, no
    // reply awaited): one group key a press, needing only a packet of this session (not a known
    // function). No target is guessed and none is confirmed: busy until the pair has left the UART, Ok
    // then, Timeout/HardwareError when it could not leave (GroupMs). The readings show what the
    // controller did; a key it ignores is not repeated. SCPI and the restore use selectFunction.
    // Once its pair has gone (or may have: a failed or stuck transmit), the commands that judge packets
    // (selectFunction, requestKey, setRange, pressRange) are refused until a packet newer than its end
    // (fresh()): the controller may apply the key later, so a packet before it proves nothing. Another
    // group key needs none (one pair a press, as the factory's).
    static constexpr uint32_t GroupMs = 250;
    bool pressGroup(CommandGroup group, const Result &latest, uint32_t nowMs, uint64_t minimumUs);
    bool requestKey(KeyCommand::Action action, const Result &latest, uint32_t nowMs, uint64_t minimumUs);
    bool setRange(unsigned index, const Result &latest, uint32_t nowMs, uint64_t minimumUs);
    // The physical AUTO/RANGE (KeyGestures press, release/releaseLate, holdRepeat): the qualified press
    // opens a NextRange stroke (its 20); the release within the factory's window ends it with 21, a later
    // one with nothing (the command is then judged from the release); each hold sends a 22 (the first
    // makes the command Auto). A press while the previous physical stroke's command only awaits its
    // packet (nothing owed on the wire) supersedes it: that command ends without a status.
    bool pressRange(const Result &latest, uint32_t nowMs, uint64_t nowUs, uint64_t minimumUs);
    bool releaseRange(bool sendRelease, uint32_t nowMs) { return stroke_.release(sendRelease, nowMs); }
    bool holdRange(uint32_t nowMs);
    bool rangeOpen() const { return stroke_.open() && !stroke_.closing(); }
    void service(CommandPort &port, const Result &latest, uint32_t nowMs);
    // Controlled cancel: every command is cancelled (none is confirmed later); a stroke whose 20 went
    // still sends its 21 (RangeStroke::close). True once nothing is owed on the wire.
    bool close(uint32_t nowMs);
    bool owes() const { return stroke_.active(); }
    // The UART stopped or failed: nothing more can be sent; everything ends without a status.
    void abandon();
    bool busy() const { return selector_.busy() || key_.busy() || range_.busy() || stroke_.active() || group_ == DataStatus::Busy; }
    // A new HOLD needs a reading after any function command already sent.
    bool functionPending(const Result &latest) const {
        return selector_.busy() || group_ == DataStatus::Busy || !fresh(latest);
    }
    DataStatus status() const {
        return operation_ == Operation::Key ? key_.status() : operation_ == Operation::Range ? range_.status() :
               operation_ == Operation::Group ? group_ : selector_.status();
    }
    // The command status() reports, for a notice: "function", "key", "range" or "group key".
    const char *operation() const {
        return operation_ == Operation::Key ? "key" : operation_ == Operation::Range ? "range" : operation_ == Operation::Group ? "group key" : "function";
    }
    // The last function choice: a selection (selectFunction) or a group key (pressGroup).
    DataStatus selectionStatus() const { return groupChosen_ ? group_ : selector_.status(); }
private:
    enum class Operation : uint8_t { Function, Key, Range, Group };
    bool idle() const { return !busy(); }
    bool startStroke(CommandGroup command, uint32_t nowMs);
    void serviceStroke(CommandPort &port, const Result &latest, uint32_t nowMs);
    void cancelOwner();
    void serviceGroup(CommandPort &port, const Result &latest, uint32_t nowMs);
    void cancelGroup() { if (group_ == DataStatus::Busy) group_ = DataStatus::NoData; }
    // Not a packet from before the last group key's end (pressGroup).
    bool fresh(const Result &latest) const { return !fenced_ || int32_t(latest.sequence - fence_) > 0; }
    FunctionSelector selector_;
    KeyCommand key_;
    RangeSelector range_;
    RangeStroke stroke_; // Owned by the Key or Range operation that started it.
    Operation operation_ = Operation::Function;
    bool physical_ = false; // The Key operation is the physical AUTO/RANGE's.
    bool ended_ = false;    // The running stroke's end went (EndSent, Released).
    CommandGroup groupKey_ = CommandGroup::Voltage; // pressGroup's key, its status and progress.
    DataStatus group_ = DataStatus::NoData;
    bool groupSent_ = false, groupChosen_ = false, fenced_ = false;
    uint32_t groupDeadline_ = 0, fence_ = 0; // fence_: the newest packet at the group key's end.
};
} // namespace openosc::dmm
