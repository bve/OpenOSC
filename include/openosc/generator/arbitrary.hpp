#pragma once
#include "openosc/generator/waveform.hpp"
#include <cstddef>
#include <cstdint>

namespace openosc::generator {
// Where decoded DAC codes go (ArbitraryBlock, ArbitraryCsv): false when the stage they fill is gone.
class CodeSink {
public:
    virtual bool put(unsigned index, uint16_t code) = 0;
protected:
    ~CodeSink() = default;
};
// Codes into an output driver's stage (Output::putArbitrary) for the owner that began it.
template <class Output> class OutputStage final : public CodeSink {
public:
    OutputStage(Output &output, const void *owner) : output_(output), owner_(owner) {}
    bool put(unsigned index, uint16_t code) override { return output_.putArbitrary(owner_, index, code); }
private:
    Output &output_;
    const void *owner_;
};
// The Arbitrary waveform's memory (GitHub #175, docs/AWG.md): two banks of ArbitraryLimits::MaximumPoints
// codes in SRAM4. ZT703S: the DAC output's banks, shared with the built tables of the other shapes (they
// never go into the loaded waveform's bank); ZT706S: the module driver's, for the waveform alone. One
// bank may hold the loaded waveform; a new one is staged by one owner at a time (a SCPI session or the
// CSV loader) in a bank that neither plays nor holds it, so the waveform in force stays intact until the
// new one is complete and validated (commit). The codes are kept as loaded on both boards. Without
// registers.
class ArbitraryBanks {
public:
    static constexpr unsigned None = 2, Points = ArbitraryLimits::MaximumPoints;
    using Table = uint16_t[Points];
    // Ready: the stage is the owner's (empty). Pending: the owner holds one already (its earlier block
    // not taken yet). Busy: another owner's. Conflict: no free bank (the output plays another shape
    // and a waveform is loaded; the ZT703S DAC only). Unsupported: a generator without the AWG (test doubles).
    enum class Stage : uint8_t { Ready, Pending, Busy, Conflict, Unsupported };
    struct Undo { unsigned bank; uint16_t points; ArbitrarySource source; };
    explicit ArbitraryBanks(Table *tables) : tables_(tables) {}
    bool loaded() const { return loaded_ != None; }
    unsigned bank() const { return loaded_; }
    uint16_t points() const { return loaded() ? points_ : 0; }
    ArbitrarySource source() const { return loaded() ? source_ : ArbitrarySource::None; }
    uint32_t revision() const { return revision_; }
    const uint16_t *samples() const { return loaded() ? tables_[loaded_] : nullptr; }
    // The bank a built table goes in, `active` the one in use and `playing` the one the DAC streams
    // (None: none): never the loaded waveform's, a stage's only when no other bank is free.
    unsigned tableBank(unsigned active, unsigned playing) const;
    // The bank `playing` never is: a stage begins only where nothing plays and nothing is loaded.
    Stage begin(const void *owner, unsigned active, unsigned playing);
    unsigned staged() const { return owner_ ? stage_ : None; }
    bool holds(const void *owner) const { return owner && owner_ == owner; }
    bool lost(const void *owner) const { return holds(owner) && lost_; }
    bool put(const void *owner, unsigned index, uint16_t code);
    void abort(const void *owner) { if (holds(owner)) { owner_ = nullptr; lost_ = false; } }
    // `bank` is needed for an output now: a stage there is lost (its owner's commit fails).
    void claim(unsigned bank) { if (owner_ && stage_ == bank) lost_ = true; }
    // The owner's stage becomes the loaded waveform; undo brings the previous one back (its bank was
    // never written meanwhile).
    bool commit(const void *owner, unsigned points, ArbitrarySource source, Undo &undo);
    void rollback(const Undo &undo);

private:
    Table *tables_;
    const void *owner_ = nullptr;
    unsigned loaded_ = None, stage_ = None;
    uint16_t points_ = 0;
    ArbitrarySource source_ = ArbitrarySource::None;
    uint32_t revision_ = 0;
    bool lost_ = false;
};
// SOURce:ARBitrary:DATA's IEEE 488.2 definite-length block: little-endian int16 codes, 0..4095 each,
// ArbitraryLimits points (ZT703S 2..1024, ZT706S 20..255). Every byte of the announced length is data (LF, CR, NUL).
class ArbitraryBlock {
public:
    // The announced length before any data: 0, or -223 (an odd count, fewer or more points than allowed).
    static int16_t check(uint32_t bytes) {
        return (bytes & 1) || !ArbitraryLimits::points(bytes / 2) ? -223 : 0;
    }
    void begin() { at_ = 0; error_ = 0; }
    void byte(uint8_t value, CodeSink &sink);
    unsigned points() const { return at_ / 2; }
    int16_t error() const { return error_; } // -222: a code outside 0..4095; -200: the stage went.
private:
    uint32_t at_ = 0;
    uint8_t low_ = 0;
    int16_t error_ = 0;
};
// A waveform file (docs/AWG.md): ASCII text, one code 0..4095 a line, LF or CRLF.
// Blank lines and lines starting with '#' are skipped; one header line (starting with a letter) may
// come before the first code; a UTF-8 byte order mark at the start is skipped. Anything else stops
// the load with its line: a NUL or other control byte, a CR without LF, a line longer than
// LineBytes, a field that is not a whole code, more than MaximumPoints codes or fewer than
// MinimumPoints, a file over MaximumBytes.
class ArbitraryCsv {
public:
    static constexpr uint32_t MaximumBytes = 65536;
    static constexpr unsigned LineBytes = 64;
    enum class Error : uint8_t { None, Binary, Line, Syntax, Range, TooMany, TooFew, Size, Stage };
    void begin() { *this = {}; }
    bool feed(const char *data, size_t size, CodeSink &sink); // False: stopped (error(), line()).
    bool finish(CodeSink &sink);                              // The end of the file.
    unsigned points() const { return points_; }
    Error error() const { return error_; }
    uint32_t line() const { return line_; }
    static const char *text(Error error);
private:
    bool fail(Error error) { error_ = error; return false; }
    bool endLine(CodeSink &sink);
    char text_[LineBytes + 1]{};
    unsigned used_ = 0, points_ = 0;
    uint32_t line_ = 1, bytes_ = 0;
    bool header_ = false, carriage_ = false, bom_ = false;
    Error error_ = Error::None;
};
} // namespace openosc::generator
