#pragma once
#include "openosc/decode/result.hpp"
#include <cstddef>
#include <cstdint>

// The decoded events of one ResultSet as a CSV file (plan §9 stage 7; format v2, v3 for DMX512, in
// docs/SCOPE_PROTOCOL_DECODERS_CSV.md): "# key=value" metadata lines, then one fixed table of events,
// one row per stored descriptor (a CONT segment is a row of its own). Pull-based and non-blocking:
// read() fills at most `capacity` bytes and returns; any split of the reads gives the same bytes.
namespace openosc::decoding::exporting {
// What the file says besides the ResultSet. Copied by begin(): nothing here must outlive it.
struct CsvContext {
    // Where the time columns count from: the trigger (signed, negative before it) or the record's
    // first sample (index 0). Chosen by the caller, not read from the set.
    enum class TimeReference : uint8_t { Start, Trigger };
    static constexpr unsigned MaxLines = 2, TextBytes = 32, RoleBytes = 8;
    // A source line of the decoder: its role ("RX", "SDA", "SCLK"), its scope channel (0 = CH1, as
    // Transition::channel) and, when the caller knows it, the threshold and its hysteresis.
    struct Line {
        const char *role = nullptr; // At most RoleBytes - 1 characters.
        uint8_t channel = 0;
        bool threshold = false;     // thresholdMv / hysteresisMv are known.
        int32_t thresholdMv = 0;
        uint32_t hysteresisMv = 0;  // The total width used (Auto may exceed INT32_MAX at a large scale).
        bool hysteresisAuto = false; // hysteresisMv came from the line's swing in the record (Auto), else Manual.
    };
    const char *firmware = nullptr; // "OpenOSC 0.7.0": at most TextBytes - 1 characters; null: no line.
    TimeReference reference = TimeReference::Start;
    uint32_t triggerIndex = 0; // The record index of the trigger (Trigger only).
    Line lines[MaxLines];
    uint8_t lineCount = 0;
};

// One export at a time. The caller holds the ResultSet immutably leased from begin() until done()
// (or end()): the stream keeps a pointer to it, never copies or changes it, and reads each event's
// elements from its pool while their row is written. Each character costs amortized O(1): the cursor
// holds the current section, event, column and element; no earlier row is ever revisited.
class DecodeCsvStream {
public:
    enum class Error : uint8_t {
        None,
        Protocol,  // The set has no protocol (Off) or an unknown one.
        Record,    // Its RecordKey is not valid.
        Unfinished, // Idle or still Decoding: not a result to export.
        Events,    // More events than the table holds.
        Descriptor, // An event that cannot be read safely (errorEvent()): kind of another protocol,
                    // unknown width, payload outside the pool, first > last, a CAN frame CanFdView refuses.
        Context,   // A firmware or role text too long, or more lines than MaxLines.
    };
    static constexpr unsigned ChunkBytes = 64;
    static constexpr unsigned Columns = 50; // v2: v1's 37 plus 13 CAN / CAN FD columns.
    static constexpr unsigned DmxColumns = Columns + 4; // v3: appends the DMX512 fields.
    static const char *column(unsigned index); // The v2 header's column names, "" past the last.

    // Checks the whole set once (O(events)) and starts the file. False: nothing will be written,
    // error() says why; a stream begun earlier is ended.
    bool begin(const ResultSet &set, const CsvContext &context);
    // The next bytes of the file, at most `capacity`. 0 only when there is nothing more (done, or not
    // begun): any capacity > 0 makes progress.
    size_t read(uint8_t *output, size_t capacity);
    bool active() const { return set_ != nullptr; }
    bool done() const { return active() && section_ == Section::Done && chunkPos_ == chunkLen_; }
    void end(); // Lets go of the set (the lease may be returned): read() then writes nothing.
    Error error() const { return error_; }
    unsigned errorEvent() const { return errorEvent_; } // The descriptor of Error::Descriptor.
    uint64_t written() const { return written_; }       // Bytes handed out so far.

private:
    enum class Section : uint8_t { Metadata, Header, Rows, Done };
    struct Line {
        char role[CsvContext::RoleBytes];
        uint8_t channel;
        bool threshold, hysteresisAuto;
        int32_t thresholdMv;
        uint32_t hysteresisMv;
    };
    void fill();             // At least one byte into the chunk, or Done.
    bool metadata();         // Writes the next metadata item; false past the last.
    bool cell();             // Writes the next piece of the current row; true when the row is complete.
    bool simple(unsigned column, const DecodeEvent &e); // A one-piece column; false: not one.
    bool lastOfGroup() const;
    unsigned columns() const { return set_->protocol() == Protocol::Dmx512 ? DmxColumns : Columns; }
    void time(uint32_t index);
    void duration(uint32_t points); // A span in seconds, as time().
    // Every piece is far below ChunkBytes (the longest, a metadata line, 44); the bound only keeps
    // the chunk in place.
    void put(char c) { if (chunkLen_ < ChunkBytes) chunk_[chunkLen_++] = c; }
    void put(const char *text);
    void hex(uint32_t value, unsigned digits);
    void integer(uint64_t value);
    void signedInteger(int64_t value);
    void escaped(uint8_t c, bool quoted);
    void keyLine(const char *key); // "# key=" of a metadata line.

    const ResultSet *set_ = nullptr;
    uint32_t origin_ = 0;
    char firmware_[CsvContext::TextBytes] = "";
    Line lines_[CsvContext::MaxLines] = {};
    uint8_t lineCount_ = 0;
    bool trigger_ = false;
    Error error_ = Error::None;
    unsigned errorEvent_ = 0;
    Section section_ = Section::Done;
    uint16_t item_ = 0, element_ = 0; // Metadata: line, character. Rows: event, column, element.
    uint16_t event_ = 0;
    uint8_t column_ = 0;
    uint8_t chunkLen_ = 0, chunkPos_ = 0;
    uint64_t written_ = 0;
    char chunk_[ChunkBytes] = {};
};
} // namespace openosc::decoding::exporting
