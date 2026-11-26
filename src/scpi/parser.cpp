#include "openosc/scpi/parser.hpp"
#include <cstring>

namespace openosc::scpi {
namespace {
bool space(char c) { return c == ' ' || c == '\t'; }
char upper(char c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; }
}
void Parser::reset() {
    endBlock(-161);
    used_ = head_ = count_ = 0; path_[0] = 0; quote_ = 0; error_ = 0;
}
void Parser::endBlock(int16_t error) {
    if (begun_ && error && sink_) sink_->blockAbort();
    begun_ = false; block_ = Block::None; digits_ = seen_ = 0; length_ = remaining_ = 0; blockError_ = 0;
}
// The '#' of a block only right after the header and its separator: nothing of the argument yet.
bool Parser::argumentStart() const {
    size_t i = 0;
    while (i < used_ && space(line_[i])) ++i;
    if (i == used_) return false;
    while (i < used_ && !space(line_[i])) ++i;
    if (i == used_) return false;
    while (i < used_ && space(line_[i])) ++i;
    return i == used_;
}
bool Parser::blockByte(uint8_t byte) {
    switch (block_) {
    case Block::Data:
        sink_->blockByte(byte);
        if (!--remaining_) block_ = Block::After;
        return true;
    case Block::Skip:
        if (!--remaining_) block_ = Block::After;
        return true;
    case Block::Discard: return true; // Up to the end of the message (endMessage) or a reset.
    case Block::After:
        if (byte == '\n' || byte == ';') return finishBlock(byte == '\n');
        if (byte == ' ' || byte == '\t' || byte == '\r') return true;
        endBlock(-161); error_ = -161; // Anything else after the data: the command is not taken.
        return true;
    case Block::Length: {
        if (byte < '0' || byte > '9') { endBlock(-161); block_ = Block::Discard; error_ = -161; return true; }
        // Taking the stage depends on the generator state after any earlier commands, such as OUTP OFF.
        if (seen_ + 1 == digits_ && count_) return false;
        const uint32_t length = length_ * 10 + uint32_t(byte - '0');
        if (++seen_ < digits_) { length_ = length; return true; }
        line_[used_] = 0;
        char path[sizeof path_];
        std::memcpy(path, path_, sizeof path);
        // The queue is empty. Prepare its first slot now and publish it only when the block ends.
        auto &command = commands_[head_];
        command = {};
        decode(command);
        int16_t result = command.error ? command.error : sink_ ? sink_->blockBegin(command.header, length) : int16_t(-168);
        if (result == BlockSink::Wait) { std::memcpy(path_, path, sizeof path); --seen_; return false; } // This digit again once the sink is free.
        if (result > 0) result = -200;
        length_ = remaining_ = length;
        begun_ = !result; blockError_ = result;
        block_ = !length ? Block::After : result ? Block::Skip : Block::Data;
        return true;
    }
    default: // Hash: the digit count.
        if (byte >= '1' && byte <= '9') { digits_ = byte - '0'; seen_ = 0; length_ = 0; block_ = Block::Length; return true; }
        if (byte == '0') { block_ = Block::Discard; error_ = -161; return true; } // Indefinite: its end is unknown.
        block_ = Block::None; // Not a block (#H1F): the '#' is the argument's.
        if (used_ + 1 >= sizeof line_) { error_ = -363; return feed(byte); }
        line_[used_++] = '#';
        return feed(byte);
    }
}
bool Parser::finishBlock(bool end) {
    auto &command = commands_[head_];
    command.error = blockError_;
    command.block = !blockError_;
    ++count_;
    begun_ = false; // Taken as a command: its stage is the command's now.
    endBlock(0);
    used_ = 0; quote_ = 0; error_ = 0;
    if (end) path_[0] = 0;
    return true;
}
bool Parser::endMessage() {
    switch (block_) {
    case Block::None: return feed('\n');
    case Block::After: return finishBlock(true);
    case Block::Hash:
        block_ = Block::None;
        if (used_ + 1 < sizeof line_) line_[used_++] = '#'; else error_ = -363;
        return feed('\n');
    default: // The message ended inside the block (or after #0 / a malformed length): -161.
        if (count_ == 4) return false;
        endBlock(-161);
        error_ = -161;
        return finish(true);
    }
}
bool Parser::take(Command &command) {
    if (!count_) return false;
    command = commands_[head_]; head_ = (head_ + 1) % 4; --count_; return true;
}
bool Parser::feed(uint8_t byte) {
    if (block_ != Block::None) return blockByte(byte);
    if (byte == '#' && !quote_ && !error_ && argumentStart()) { block_ = Block::Hash; return true; }
    if (byte == '\n' || (!quote_ && byte == ';')) return finish(byte == '\n');
    if (byte == '\r' && !quote_) return true;
    if (error_) return true;
    if (byte < 32 && byte != '\t') { error_ = -102; return true; }
    if (byte >= 127) { error_ = -102; return true; }
    if (used_ + 1 >= sizeof line_) { error_ = -363; return true; }
    if (byte == '"' || byte == '\'') {
        if (!quote_) quote_ = byte;
        else if (quote_ == byte) quote_ = 0;
    }
    line_[used_++] = byte; return true;
}
bool Parser::finish(bool end) {
    if (count_ == 4 && (used_ || error_)) return false;
    if (used_ || error_) {
        auto &command = commands_[(head_ + count_) % 4]; command = {};
        command.error = error_ ? error_ : quote_ ? -102 : 0;
        if (!command.error) { line_[used_] = 0; decode(command); }
        ++count_;
    }
    used_ = 0; quote_ = 0; error_ = 0;
    if (end) path_[0] = 0;
    return true;
}
void Parser::decode(Command &command) {
    const char *begin = line_;
    while (space(*begin)) ++begin;
    const bool root = *begin == ':';
    if (root) ++begin;
    const char *argument = begin;
    while (*argument && !space(*argument)) ++argument;
    size_t length = argument - begin;
    if (!length) { command.error = -102; return; }
    if (begin[length - 1] == '?') { command.query = true; --length; }
    const size_t prefix = !root && *begin != '*' ? std::strlen(path_) : 0;
    if (!length || prefix + length >= sizeof command.header) { command.error = -113; return; }
    std::memcpy(command.header, path_, prefix);
    for (size_t i = 0; i < length; ++i) {
        const char c = upper(begin[i]);
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ':' || (c == '*' && i == 0))) {
            command.error = -102; return;
        }
        command.header[prefix + i] = c;
    }
    command.header[prefix + length] = 0;
    if (*begin != '*') {
        const char *colon = std::strrchr(command.header, ':');
        const size_t size = colon ? colon - command.header + 1 : 0;
        std::memcpy(path_, command.header, size); path_[size] = 0;
    }
    while (space(*argument)) ++argument;
    size_t bytes = std::strlen(argument);
    while (bytes && space(argument[bytes - 1])) --bytes;
    if (bytes >= sizeof command.argument) { command.error = -363; return; }
    std::memcpy(command.argument, argument, bytes); command.argument[bytes] = 0;
}
} // namespace openosc::scpi
