#include "openosc/application/scpi_backend.hpp"
#include "openosc/application/controller.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/scpi/parser.hpp"
#include <type_traits>

// SOURce:ARBitrary:DATA's block for the session this backend serves (AWG, docs/AWG.md): staged
// in the generator's free bank as the bytes come (Engine::feed, also inside FAT waits: only the stage's
// own bank is written), taken once the whole block is validated (the engine's dispatch).
namespace openosc {
using Stage = generator::ArbitraryBanks::Stage;
int16_t InstrumentBackend::arbitraryBegin(const void *owner, uint32_t) {
    auto &a = application_;
    if (a.bodeOwns() || a.data_.zeroBusy()) return -221; // The output is another operation's now.
    switch (a.data_.generator().beginArbitrary(owner)) {
    case Stage::Ready: arbitrary_.owner = owner; arbitrary_.block.begin(); return 0;
    case Stage::Pending: return scpi::BlockSink::Wait; // This session's previous block is not taken yet.
    case Stage::Busy: return -200;                     // The other session's or the CSV loader's.
    case Stage::Conflict: return -221;                 // Another shape plays and a waveform is loaded.
    default: return -241;
    }
}
void InstrumentBackend::arbitraryByte(const void *owner, uint8_t byte) {
    if (owner != arbitrary_.owner) return;
    auto &output = application_.data_.generator();
    generator::OutputStage<std::remove_reference_t<decltype(output)>> stage(output, owner);
    arbitrary_.block.byte(byte, stage);
}
int16_t InstrumentBackend::arbitraryCommit(const void *owner) {
    auto &a = application_;
    auto &output = a.data_.generator();
    if (owner != arbitrary_.owner || !output.holdsArbitrary(owner)) return -200;
    arbitrary_.owner = nullptr;
    if (const int16_t e = arbitrary_.block.error()) { output.abortArbitrary(owner); return e; }
    if (a.bodeOwns() || a.data_.zeroBusy()) { output.abortArbitrary(owner); return -221; }
    // Lost: another shape's table took its bank meanwhile; refused: not at this rate and clock while
    // on (the previous waveform plays on).
    return a.commitArbitrary(owner, arbitrary_.block.points(), generator::ArbitrarySource::Remote, Board::milliseconds()) ? 0 : -221;
}
void InstrumentBackend::arbitraryAbort(const void *owner) {
    if (owner == arbitrary_.owner) arbitrary_.owner = nullptr;
    application_.data_.generator().abortArbitrary(owner);
}
size_t InstrumentBackend::arbitraryRead(uint32_t revision, uint32_t offset, uint8_t *out, size_t size) {
    const auto &output = application_.data_.generator();
    const auto snapshot = output.snapshot();
    const uint16_t *samples = output.arbitrarySamples();
    if (!samples || snapshot.arbitraryRevision != revision) return 0;
    const uint32_t bytes = uint32_t(snapshot.arbitraryPoints) * 2;
    size_t n = 0;
    for (; n < size && offset < bytes; ++n, ++offset) out[n] = uint8_t(samples[offset / 2] >> (offset & 1 ? 8 : 0));
    return n;
}
} // namespace openosc
