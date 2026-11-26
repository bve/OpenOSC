#pragma once
#include <cstddef>
#include <cstdint>

namespace openosc::scpi {
struct Command {
    char header[96]{}, argument[160]{};
    int16_t error = 0;
    bool query = false;
    bool block = false; // Its argument was a definite-length block, streamed to the BlockSink (argument empty).
};
// Where a definite-length block's data goes (IEEE 488.2 7.7.6.2, #<n><length><data>): byte by byte as
// it comes, never through the line buffer. blockBegin has the command's whole header and the length:
// 0 takes the block, Wait holds the input (Parser::feed false: the same byte again later), a negative
// SCPI error skips the block's bytes (the command reports that error). blockAbort: a block begun did
// not end as a command (the message ended inside it, bytes other than the terminator after it, reset).
class BlockSink {
public:
    static constexpr int16_t Wait = 1;
    virtual int16_t blockBegin(const char *header, uint32_t bytes) = 0;
    virtual void blockByte(uint8_t byte) = 0;
    virtual void blockAbort() = 0;
protected:
    ~BlockSink() = default;
};
class Parser {
public:
    void sink(BlockSink *sink) { sink_ = sink; }
    bool feed(uint8_t byte); // False applies backpressure; retry the same byte.
    bool take(Command &command);
    void reset();
    // The end of a message (USBTMC EOM): also inside a block, where LF is data.
    bool endMessage();
    // Inside a block (its count, data or what follows it): an LF there is not the end of the input.
    bool inBlock() const { return block_ != Block::None; }
    // An indefinite (#0) or malformed length: everything up to the end of the message is dropped, as
    // its extent is unknown (no binary data is ever taken as commands).
    bool discarding() const { return block_ == Block::Discard; }
private:
    // Hash: "#" begins the argument (its digit count next). Length: the length digits. Data: to
    // the sink. Skip: refused, counted to its end. After: only spaces, CR, then LF or ';'.
    enum class Block : uint8_t { None, Hash, Length, Data, Skip, After, Discard };
    bool finish(bool end);
    bool finishBlock(bool end);
    bool blockByte(uint8_t byte);
    bool argumentStart() const;
    void endBlock(int16_t error); // The block's state cleared; a begun one aborted when error.
    void decode(Command &command);
    char line_[256]{}, path_[96]{};
    size_t used_ = 0;
    Command commands_[4];
    unsigned head_ = 0, count_ = 0;
    char quote_ = 0;
    int16_t error_ = 0;
    BlockSink *sink_ = nullptr;
    Block block_ = Block::None;
    bool begun_ = false;            // The sink took the block (blockAbort owed if it does not end as a command).
    unsigned digits_ = 0, seen_ = 0;
    uint32_t length_ = 0, remaining_ = 0;
    int16_t blockError_ = 0;
};
} // namespace openosc::scpi
