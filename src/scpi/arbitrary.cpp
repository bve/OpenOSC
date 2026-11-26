#include "openosc/scpi/engine.hpp"
#include "openosc/generator/arbitrary.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

// The Arbitrary waveform over SCPI (GitHub #175, docs/AWG.md), both boards within ArbitraryLimits:
//   SOURce:ARBitrary:DATA <block>   codes 0..4095, little-endian int16 (ZT703S 2..1024, ZT706S 20..255)
//   SOURce:ARBitrary:DATA?          the loaded waveform as the same block, the codes as loaded (#10: none)
//   SOURce:ARBitrary:SRATe <S/s>|?  the sample rate asked (ZT703S: 1 S/s .. the DAC ceiling at this
//                                   clock; ZT706S: 1 S/s .. 200 MS/s nominal)
//   SOURce:ARBitrary:POINts?        its points (0: none loaded)
//   SOURce:ARBitrary:SOURce?        NONE, FILE (a CSV from /openosc/AWG) or REMote (a block)
// SOUR:FUNC ARB selects it; SOUR:FREQ then sets the rate as frequency x points. A block or a rate that
// sends a ZT706S module frame completes with the module's acknowledge (waitGenerator: -240 without).
namespace openosc::scpi {
namespace {
using Limits = generator::ArbitraryLimits;
}
int16_t Engine::blockBegin(const char *header, uint32_t bytes) {
    if (!Header::matches(header, "SOURce:ARBitrary:DATA")) return -168;
    if (const int16_t e = generator::ArbitraryBlock::check(bytes)) return e;
    return backend_.arbitraryBegin(this, bytes);
}
bool Engine::arbitraryCommand(const Command &c) {
    const auto match = [&](const char *pattern) { return Header::matches(c.header, pattern); };
    const bool data = match("SOURce:ARBitrary:DATA"), rate = match("SOURce:ARBitrary:SRATe");
    const bool points = match("SOURce:ARBitrary:POINts"), source = match("SOURce:ARBitrary:SOURce");
    if (!data && !rate && !points && !source) return false;
    const auto snapshot = backend_.generator();
    if (data && c.block) { // The block is staged already: taken as a whole, or nothing changes.
        if (c.query) { dropBlock(); error(-108, true); return true; }
        if (const int16_t e = backend_.arbitraryCommit(this)) error(e);
        else waitGenerator(); // A ZT706S module playing it acknowledges the new frame.
        return true;
    }
    if (data) {
        if (!c.query) { error(*c.argument ? -104 : -109); return true; } // Only a definite block.
        const uint32_t bytes = uint32_t(snapshot.arbitraryPoints) * 2;
        char count[16]; std::snprintf(count, sizeof count, "%lu", (unsigned long)bytes);
        reply("#%u%s", unsigned(std::strlen(count)), count);
        arbitraryRevision_ = snapshot.arbitraryRevision; arbitraryAt_ = 0; arbitraryBytes_ = bytes; arbitraryBinary_ = bytes != 0;
        return true;
    }
    if ((points || source) && !c.query) { error(-113); return true; } // Queries only.
    if (points) { reply("%u", unsigned(snapshot.arbitraryPoints)); return true; }
    if (source) {
        constexpr const char *names[] = {"NONE", "FILE", "REM"};
        reply("%s", names[unsigned(snapshot.arbitrarySource) % 3]);
        return true;
    }
    auto settings = snapshot.requested;
    if (c.query) { reply("%lu", (unsigned long)settings.arbitrarySamplesPerSecond); return true; }
    double value;
    if (!argument(c, value)) return true;
    if (value != std::floor(value) || !(value >= 0) || value > 4294967295.0 || !Limits::rate(uint32_t(value), snapshot.timerHz)) { error(-222); return true; }
    settings.arbitrarySamplesPerSecond = uint32_t(value);
    if (!backend_.configureGenerator(settings)) error(-222);
    else waitGenerator();
    return true;
}
} // namespace openosc::scpi
