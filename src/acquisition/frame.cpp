#include "openosc/acquisition/frame.hpp"
#include "openosc/acquisition/adc_bus_glitch.hpp"
#include "openosc/acquisition/timebases.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#if defined(__ARM_FEATURE_DSP)
#include "stm32h7xx.h" // CMSIS-Core: the Cortex-M7 SIMD intrinsics.
#include <arm_acle.h>  // ACLE: the same instructions, not volatile, for those that leave the GE flags unread.
#define OPENOSC_PAIR_SIMD 1
#elif defined(OPENOSC_DSP_SIMD_EMULATION)
#include "simd_emulation.hpp" // Native test only: the same intrinsics, modelled on a host.
#define OPENOSC_PAIR_SIMD 1
#endif

namespace openosc::scope {
bool FrameMetadata::valid() const {
    if ((timebaseNs && (timebaseNs < 10 || timebaseNs > Timebases::Maximum)) ||
        !samples || samples > ring || !ring || ring > MaximumRing || !samplesPerSecond.valid() ||
        static_cast<unsigned>(layout) > static_cast<unsigned>(Layout::FourByteBanks))
        return false;
    if (!channels[0].enabled && !channels[1].enabled)
        return false;
    if (timebaseNs && timebaseNs/100000000.0*samplesPerSecond.value()>2.0*MaximumRing)
        return false; // Bound fixed-point projection arithmetic even for hostile imports.
    if ((rolling && (!timebaseNs || triggered)) || (triggered && triggerIndex >= samples))
        return false;
    if (pretriggerKnown && (rolling || pretrigger >= samples)) return false;
    // The pretrigger point of a shifted trigger lies in the record.
    if (triggerShift && (!triggered || int64_t(triggerIndex) - triggerShift < 0 || int64_t(triggerIndex) - triggerShift >= samples))
        return false;
    // One channel through both ADCs, its points in pairs of a word; only it carries a correction.
    if (layout == Layout::InterleavedCh1 && (channels[1].enabled || ring % 2))
        return false;
    if (!InterleaveCorrection::valid(interleaveOffset) || (interleaveOffset != 0 && layout != Layout::InterleavedCh1))
        return false;
    if (interleaveMatch && (layout != Layout::InterleavedCh1 || rolling || !interleaveMatch->valid()))
        return false;
    if (layout == Layout::FourByteBanks && channels[0].enabled == channels[1].enabled)
        return false; // Byte lanes hold exactly one channel.
    for (auto c : channels)
        if (!c.probeNumerator || !c.probeDenominator || !c.millivoltsPerDivision ||
            c.analogPosition < 0 || c.analogPosition > 160 ||
            static_cast<unsigned>(c.coupling) > 1 || !std::isfinite(c.voltsPerCode) ||
            !std::isfinite(c.zeroCode) || (c.calibrated && c.voltsPerCode <= 0))
            return false;
    return true;
}
FrameView::FrameView(const FrameMetadata &metadata, const uint16_t *const *banks, size_t wordsPerBank,
                     const SplitHistory *split)
    : metadata_(metadata), wordsPerBank_(wordsPerBank) {
    if (banks)
        for (unsigned i = 0; i < 4; ++i)
            banks_[i] = banks[i];
    if (split) split_ = *split;
    valid_ = validate();
    evenShift_ = valid_ ? InterleaveCorrection::shift(metadata_.interleaveOffset) : 0;
    if (valid_) {
        const float offsetQ8 = metadata_.interleaveOffset * 256;
        evenShiftQ8_ = int16_t(offsetQ8 + (offsetQ8 < 0 ? -0.5f : 0.5f));
        if (metadata_.interleaveMatch)
            matchBiasQ23_ = metadata_.interleaveMatch->bias(
                evenShiftQ8_, int32_t(std::lround(std::clamp(metadata_.channels[0].zeroCode, -128.0, 127.0) * 256)));
    }
}
bool FrameView::validate() const {
    if (!metadata_.valid())
        return false;
    if (split_.active()) {
        // The lane ring is the capture ring plus the history; SplitHistory::word relies on each limit.
        const uint32_t ahb = split_.ahbWords, history = split_.historyWords, lane = laneRing();
        if ((metadata_.layout != Layout::FourBanks && metadata_.layout != Layout::InterleavedCh1) ||
            metadata_.ring % (4 * pointsPerWord(metadata_.layout)) || !ahb || (ahb & (ahb - 1)) ||
            history % ahb || lane != ahb + history || wordsPerBank_ < ahb)
            return false;
        for (unsigned i = 0; i < 4; ++i)
            if (!banks_[i] || !split_.older[i] || split_.end[i] >= lane || split_.newest[i] >= history)
                return false;
        return true;
    }
    const unsigned count = metadata_.layout == Layout::Linear ? 1 : 4;
    const size_t ring = metadata_.ring;
    size_t words = (ring + count - 1) / count; // One word per point in the fullest bank.
    if (metadata_.layout == Layout::InterleavedCh1) words = ((ring + 1) / 2 + count - 1) / count;
    if (metadata_.layout == Layout::FourByteBanks) words = ((ring + 3) / 4 + 1) / 2; // One byte per point.
    if (wordsPerBank_ < words)
        return false;
    for (unsigned i = 0; i < count; ++i)
        if (!banks_[i])
            return false;
    return true;
}
__attribute__((always_inline)) inline uint8_t FrameView::byteAt(unsigned channel, size_t index) const {
    // Factory-compatible uint32 addition precedes ring reduction.
    uint32_t position = uint32_t(metadata_.origin + uint32_t(index)) % metadata_.ring;
    return corrected(byteAtPosition(channel, position), position);
}
// Out of line so the per-sample path of code() stays free of the rare neighbour reads. The test is
// on the captured byte (a bus pattern); the neighbours and the result are codes as the record reads
// them, so an InterleavedCh1 point resolved from its neighbours is not corrected a second time.
__attribute__((noinline, cold)) uint8_t FrameView::resolveGlitch(unsigned channel, size_t index, uint8_t raw) const {
    const uint32_t position = uint32_t(metadata_.origin + uint32_t(index)) % metadata_.ring;
    if (metadata_.samples < 2) return corrected(raw, position);
    const uint8_t before = byteAt(channel, index ? index - 1 : index + 1),
                  after = byteAt(channel, index + 1 < metadata_.samples ? index + 1 : index - 1);
    return AdcBusGlitch::stands(before, after) ? corrected(raw, position) : AdcBusGlitch::resolve(raw, before, after);
}
bool FrameView::code(Channel channel, size_t index, uint8_t &output) const {
    const unsigned c = static_cast<unsigned>(channel);
    if (!valid() || c > 1 || index >= metadata_.samples || !metadata_.channels[c].enabled ||
        (metadata_.layout == Layout::InterleavedCh1 && c))
        return false;
    const uint32_t position = uint32_t(metadata_.origin + uint32_t(index)) % metadata_.ring;
    if (__builtin_expect(metadata_.interleaveMatch && !(position & 1), 0)) {
        output = uint8_t(matchedCode(uint32_t(index), position));
        return true;
    }
    const uint8_t raw = byteAtPosition(c, position);
    output = __builtin_expect(AdcBusGlitch::suspect(raw), 0) ? resolveGlitch(c, index, raw) : corrected(raw, position);
    return true;
}
bool FrameView::signedCode(Channel channel, size_t index, int16_t &output) const {
    uint8_t raw;
    if (!code(channel, index, raw))
        return false;
    output = raw < 128 ? raw : int16_t(raw) - 256;
    return true;
}
bool FrameView::interleavedQ8(uint32_t index, int32_t &output) const {
    if (!valid_ || metadata_.layout != Layout::InterleavedCh1 || index >= metadata_.samples) return false;
    const uint32_t position = uint32_t(metadata_.origin + index) % metadata_.ring;
    if (metadata_.interleaveMatch && !(position & 1) && matchInterleaveQ8(index, output)) return true;
    const uint8_t raw = byteAtPosition(0, position);
    if (__builtin_expect(AdcBusGlitch::suspect(raw), 0)) {
        const uint8_t resolved = resolveGlitch(0, index, raw);
        if (resolved != corrected(raw, position)) {
            output = int32_t(int8_t(resolved)) * 256;
            return true;
        }
    }
    const int32_t q8 = int32_t(int8_t(raw)) * 256 + ((position & 1) ? 0 : evenShiftQ8_);
    output = std::clamp(q8, int32_t(-128 * 256), int32_t(127 * 256));
    return true;
}
namespace {
__attribute__((always_inline)) inline int32_t matchPair(uint16_t word, const int16_t *taps, int32_t sum) {
#if defined(OPENOSC_PAIR_SIMD)
    uint32_t coefficients;
    std::memcpy(&coefficients, taps, sizeof(coefficients));
    const uint32_t samples = __sxtb16(word | (uint32_t(word) << 8)); // ADC A (odd) occupies the low half.
    return __smladx(samples, coefficients, sum);
#else
    return sum + int32_t(int8_t(word >> 8)) * taps[0] + int32_t(int8_t(word)) * taps[1];
#endif
}
int32_t roundSigned(int32_t value, int32_t unit) {
    return value < 0 ? -((-value + unit / 2) / unit) : (value + unit / 2) / unit;
}
} // namespace
__attribute__((noinline, cold)) bool FrameView::matchSuspect(uint8_t raw, uint32_t index, uint32_t position) const {
    return raw == 0x7f || (raw == 0x3f && resolveGlitch(0, index, raw) != corrected(raw, position));
}
__attribute__((noinline)) bool FrameView::matchInterleaveQ8(uint32_t index, int32_t &output) const {
    constexpr uint32_t radius = InterleaveMatch::Radius;
    // Retain the captured ends and clipped waveforms. A stencil must stay inside the record.
    if (index < radius || metadata_.samples - index <= radius) return false;
    const uint32_t first = index - radius;
    uint32_t logical = uint32_t(metadata_.origin + first), at = logical % metadata_.ring;
    const auto *taps = metadata_.interleaveMatch->q15;
    int32_t sum = 0;
    int8_t center = 0;
    // Accumulate on the original bytes. The fractional even shift and zero baseline are
    // constants in this linear region; fold them into a single Q23 bias after the MACs.
    for (unsigned i = 0; i < 2 * radius; i += 2) {
        const uint16_t word = interleaveWordAt(at >> 1);
        const auto even = uint8_t(word >> 8), odd = uint8_t(word);
        const int32_t evenQ8 = int32_t(int8_t(even)) * 256 + evenShiftQ8_;
        if (even == 0x80 || odd == 0x80 || uint32_t(evenQ8 + 128 * 256) > 255 * 256)
            return false;
        if (__builtin_expect(AdcBusGlitch::suspectWord(word), 0) &&
            (matchSuspect(even, first + i, at) || matchSuspect(odd, first + i + 1, at + 1)))
            return false;
        if (i == radius) center = int8_t(even);
        sum = matchPair(word, taps + i, sum);
        logical += 2;
        at += 2;
        if (!logical || at == metadata_.ring) at = 0;
    }
    const uint8_t last = uint8_t(interleaveWordAt(at >> 1) >> 8);
    const int32_t lastQ8 = int32_t(int8_t(last)) * 256 + evenShiftQ8_;
    if (last == 0x80 || uint32_t(lastQ8 + 128 * 256) > 255 * 256 ||
        (__builtin_expect(AdcBusGlitch::suspect(last), 0) && matchSuspect(last, first + 2 * radius, at)))
        return false;
    sum += int32_t(int8_t(last)) * taps[2 * radius];
    const int32_t delta = roundSigned(sum * 256 + matchBiasQ23_, InterleaveMatch::Unity);
    output = std::clamp(center * 256 + evenShiftQ8_ - delta, int32_t(-128 * 256), int32_t(127 * 256));
    return true;
}
int16_t FrameView::matchedCode(uint32_t index, uint32_t position) const {
    int32_t q8;
    if (matchInterleaveQ8(index, q8)) return int16_t(roundSigned(q8, 256));
    const uint8_t raw = byteAtPosition(0, position);
    return int8_t(AdcBusGlitch::suspect(raw) ? resolveGlitch(0, index, raw) : corrected(raw, position));
}
uint32_t FrameView::interleaveRails(uint32_t first, uint32_t count, uint32_t limit) const {
    if (!valid_ || metadata_.layout != Layout::InterleavedCh1 || first >= metadata_.samples)
        return 0;
    const uint32_t end = first + std::min(count, metadata_.samples - first);
    uint32_t rails = 0;
    for (uint32_t index = first; index < end; ++index) {
        const uint32_t position = uint32_t(metadata_.origin + index) % metadata_.ring;
        const uint8_t raw = byteAtPosition(0, position);
        if (raw == 0x80 || (raw == 0x7f && resolveGlitch(0, index, raw) == corrected(raw, position))) {
            if (++rails >= limit) break;
        }
    }
    return rails;
}
bool FrameView::volts(Channel channel, size_t index, double &output) const {
    int16_t raw;
    if (!signedCode(channel, index, raw))
        return false;
    return metadata_.channels[static_cast<unsigned>(channel)].volts(raw, output);
}
bool ChannelMetadata::volts(int16_t code, double &output) const {
    if (!calibrated)
        return false;
    output = (code - zeroCode) * voltsPerCode * probeNumerator / probeDenominator;
    return std::isfinite(output);
}
namespace {
// One point of both channels in one register: CH1 in the low halfword, CH2 in the high, as
// SXTB16 of a word holding CH1 in byte 0 and CH2 in byte 2.
__attribute__((always_inline)) inline uint32_t spread(uint16_t word) { return word | uint32_t(word) << 8; }
#if defined(OPENOSC_PAIR_SIMD)
__attribute__((always_inline)) inline uint32_t codes(uint32_t spread) { return __SXTB16(spread); }
// Per halfword: low/high move only on a strict improvement (an equal extreme keeps the earlier
// index), and lowAt/highAt take that point's packed index. SSUB16 sets the GE flags SEL reads;
// both are volatile asm in CMSIS, so their order is kept.
__attribute__((always_inline)) inline void extremes(uint32_t c, uint32_t at, uint32_t &low, uint32_t &lowAt,
                                                    uint32_t &high, uint32_t &highAt) {
    __SSUB16(c, low); low = __SEL(low, c); lowAt = __SEL(lowAt, at);
    __SSUB16(high, c); high = __SEL(high, c); highAt = __SEL(highAt, at);
}
// The group test below reads no GE flags: ACLE lets the compiler schedule it and keep it in registers.
// Bytes 0 and 2 of a word, CH1 of its two points, as two int16 (oddCodes below: bytes 1 and 3, CH2).
__attribute__((always_inline)) inline uint32_t evenCodes(uint32_t words) { return __sxtb16(words); }
// Per halfword: a - b (codes, no overflow), and how far an unsigned a exceeds b (0 if not).
__attribute__((always_inline)) inline uint32_t sub16(uint32_t a, uint32_t b) { return __ssub16(a, b); }
__attribute__((always_inline)) inline uint32_t excess16(uint32_t a, uint32_t b) { return __uqsub16(a, b); }
__attribute__((always_inline)) inline uint32_t add16(uint32_t a, uint32_t b) { return __sadd16(a, b); }
#else
__attribute__((always_inline)) inline uint32_t codes(uint32_t spread) {
    return uint16_t(int16_t(int8_t(spread))) | uint32_t(uint16_t(int16_t(int8_t(spread >> 16)))) << 16;
}
__attribute__((always_inline)) inline void extremes(uint32_t c, uint32_t at, uint32_t &low, uint32_t &lowAt,
                                                    uint32_t &high, uint32_t &highAt) {
    for (unsigned shift : {0U, 16U}) {
        const uint32_t mask = 0xFFFFU << shift;
        const int16_t code = int16_t(c >> shift);
        if (code < int16_t(low >> shift)) { low = (low & ~mask) | (c & mask); lowAt = (lowAt & ~mask) | (at & mask); }
        if (code > int16_t(high >> shift)) { high = (high & ~mask) | (c & mask); highAt = (highAt & ~mask) | (at & mask); }
    }
}
__attribute__((always_inline)) inline uint32_t evenCodes(uint32_t words) { return codes(words); }
__attribute__((always_inline)) inline uint32_t sub16(uint32_t a, uint32_t b) {
    return uint16_t(a - b) | uint32_t(uint16_t((a >> 16) - (b >> 16))) << 16;
}
__attribute__((always_inline)) inline uint32_t add16(uint32_t a, uint32_t b) {
    return uint16_t(a + b) | uint32_t(uint16_t((a >> 16) + (b >> 16))) << 16;
}
__attribute__((always_inline)) inline uint32_t excess16(uint32_t a, uint32_t b) {
    uint32_t out = 0;
    for (unsigned shift : {0U, 16U}) {
        const uint16_t x = uint16_t(a >> shift), y = uint16_t(b >> shift);
        out |= uint32_t(x > y ? x - y : 0) << shift;
    }
    return out;
}
#endif
// GCC makes neither PKHBT/PKHTB nor SXTB16 with a rotation from C, so on the device these are the
// instructions themselves (not volatile: free to schedule). Elsewhere the same in C.
#if defined(__ARM_FEATURE_DSP)
__attribute__((always_inline)) inline uint32_t oddCodes(uint32_t words) {
    uint32_t out;
    __asm__("sxtb16 %0, %1, ror #8" : "=r"(out) : "r"(words));
    return out;
}
// The low (high) halfwords of a and b as one point of both channels, a's in the low halfword.
__attribute__((always_inline)) inline uint32_t lowHalves(uint32_t a, uint32_t b) {
    uint32_t out;
    __asm__("pkhbt %0, %1, %2, lsl #16" : "=r"(out) : "r"(a), "r"(b));
    return out;
}
__attribute__((always_inline)) inline uint32_t highHalves(uint32_t a, uint32_t b) {
    uint32_t out;
    __asm__("pkhtb %0, %1, %2, asr #16" : "=r"(out) : "r"(b), "r"(a));
    return out;
}
#else
__attribute__((always_inline)) inline uint32_t oddCodes(uint32_t words) { return evenCodes(words >> 8 | words << 24); }
__attribute__((always_inline)) inline uint32_t lowHalves(uint32_t a, uint32_t b) { return (a & 0xFFFFU) | b << 16; }
__attribute__((always_inline)) inline uint32_t highHalves(uint32_t a, uint32_t b) { return a >> 16 | (b & 0xFFFF0000U); }
#endif
// suspectWord() on two words at once: nonzero exactly when one of the four bytes is 0x7F or 0x3F,
// that is when (byte | 0x40) ^ 0x7F is zero.
__attribute__((always_inline)) inline uint32_t suspectBytes(uint32_t words) {
    const uint32_t z = (words | 0x40404040U) ^ 0x7F7F7F7FU;
#if defined(OPENOSC_PAIR_SIMD)
    return __uqsub8(0x01010101U, z); // 1 in each zero byte.
#else
    return (z - 0x01010101U) & ~z & 0x80808080U; // The exact zero-byte test.
#endif
}
__attribute__((always_inline)) inline bool suspectPair(uint32_t words) { return suspectBytes(words) != 0; }
__attribute__((always_inline)) inline uint32_t load32(const uint16_t *at) {
    uint32_t v;
    __builtin_memcpy(&v, at, 4);
    return v;
}
// The extremes so far as bounds for a group of eight points: `low` and `high - low`, packed CH1 |
// CH2 << 16. A code c is strictly outside exactly when c - low, taken unsigned, exceeds high - low
// (below low it wraps past 0x8000).
struct GroupBounds { // Forced inline: -Os would outline these calls from the hot loop.
    uint32_t low, range;
    __attribute__((always_inline)) void set(uint32_t packedLow, uint32_t packedHigh) { low = packedLow; range = sub16(packedHigh, packedLow); }
    // One word of every bank (bytes 0/1: CH1/CH2 of one point, 2/3 of the point four later) into the
    // group's packed sums (|8 x 128| per halfword fits); `flags` gets a nonzero bit when the word has
    // a suspect byte or a code outside the bounds.
    __attribute__((always_inline)) void add(uint32_t words, uint32_t &sums, uint32_t &flags) const {
        const uint32_t ch1 = evenCodes(words), ch2 = oddCodes(words);
        const uint32_t first = lowHalves(ch1, ch2), second = highHalves(ch1, ch2);
        sums = add16(add16(sums, first), second);
        flags |= suspectBytes(words) | excess16(sub16(first, low), range) | excess16(sub16(second, low), range);
        // No instruction: keeps the compiler from reassociating the four words' tests into one OR
        // tree at the end of the group, which held (and spilled) every partial result until then.
        __asm__("" : "+r"(flags));
    }
};
// The FourBanks hot loop of groups that change no extreme: from p (a multiple of 8) while eight
// points remain and none of a group is suspect or outside `bounds`. Returns where it stopped (a group
// boundary); its points are in the sums, and their extremes and indexes are those already held. Out
// of line, with only this state, so that all of it stays in registers.
__attribute__((noinline)) uint32_t groupRun(const uint16_t *const (&banks)[4], uint32_t p, uint32_t end, GroupBounds bounds,
                                            int32_t &sum0, int32_t &sum1) {
    const uint16_t *const b0 = banks[0], *const b1 = banks[1], *const b2 = banks[2], *const b3 = banks[3];
    int32_t s0 = sum0, s1 = sum1;
    for (; end - p >= 8; p += 8) {
        const uint32_t k = p >> 2; // Even: words k and k + 1 of every bank, one aligned read each.
        uint32_t sums = 0, flags = 0;
        bounds.add(load32(b0 + k), sums, flags); bounds.add(load32(b1 + k), sums, flags);
        bounds.add(load32(b2 + k), sums, flags); bounds.add(load32(b3 + k), sums, flags);
        if (__builtin_expect(flags != 0, 0)) break;
        s0 += int16_t(sums); s1 += int32_t(sums) >> 16;
    }
    sum0 = s0; sum1 = s1;
    return p;
}
// Points of one block (at most 65535, so each packed index fits its halfword).
struct PairBlock {
    uint32_t low, high, lowAt, highAt, at; // Packed CH1 | CH2 << 16; `at` is the next point's index.
    int32_t sum[2];
};
// The hot loop: positions [p, end) until a suspect word, which it leaves unread for the exact path.
// Out of line, with its state in locals, so that it makes no call and keeps all of it in registers.
template <class Read> __attribute__((noinline)) uint32_t pairRun(Read read, uint32_t p, uint32_t end, PairBlock &block) {
    uint32_t low = block.low, high = block.high, lowAt = block.lowAt, highAt = block.highAt, at = block.at;
    int32_t sum0 = block.sum[0], sum1 = block.sum[1];
    for (; p != end; ++p) {
        const uint16_t word = read(p);
        if (__builtin_expect(AdcBusGlitch::suspectWord(word), 0)) break;
        const uint32_t c = codes(spread(word));
        extremes(c, at, low, lowAt, high, highAt);
        sum0 += int16_t(c); sum1 += int32_t(c) >> 16;
        at += 0x00010001U;
    }
    block = {low, high, lowAt, highAt, at, {sum0, sum1}};
    return p;
}
// FourBanks without split history: point p is word p >> 2 of bank p & 3.
struct FourBanksRead {
    const uint16_t *banks[4];
    __attribute__((always_inline)) uint16_t operator()(uint32_t p) const { return banks[p & 3][p >> 2]; }
};
// FourBanks: points in record order as the generic loop gives them, but where the banks allow in
// groups of eight, each bank read once for two words. Groups that change no extreme go through
// groupRun; a group that does takes the exact per-point path; a group with a suspect byte, unaligned
// banks and the ends go point by point, and a suspect point ends the run as in the generic loop.
__attribute__((noinline)) uint32_t pairRun(const FourBanksRead &read, uint32_t p, uint32_t end, PairBlock &block) {
    uint32_t low = block.low, high = block.high, lowAt = block.lowAt, highAt = block.highAt, at = block.at;
    int32_t sum0 = block.sum[0], sum1 = block.sum[1];
    const uint16_t *const b0 = read.banks[0], *const b1 = read.banks[1], *const b2 = read.banks[2], *const b3 = read.banks[3];
    const bool aligned = ((uintptr_t(b0) | uintptr_t(b1) | uintptr_t(b2) | uintptr_t(b3)) & 3) == 0;
    // One point in record order: its extremes with their first indexes, and the sums.
    const auto step = [&](uint16_t word) __attribute__((always_inline)) {
        const uint32_t c = codes(spread(word));
        extremes(c, at, low, lowAt, high, highAt);
        at += 0x00010001U;
        sum0 += int16_t(c); sum1 += int32_t(c) >> 16;
    };
    // A slope across the column widens an extreme in every group, so groupRun would stop at once
    // each time: after a try that took no group, the next 1, 2, 4, then 8 groups go straight to the
    // exact path; a try that takes one starts over. The result is the same either way.
    uint32_t backoff = 0, exactGroups = 0;
    while (p != end) {
        if (aligned && !(p & 7) && end - p >= 8) {
            if (exactGroups) {
                --exactGroups;
            } else {
                // Sums need no order, and a group with no point strictly below `low` or above `high`
                // changes neither nor their first indexes (ties keep the earlier one): on plateaus and
                // between edges groupRun only adds them up. Its bounds are the extremes now, so a point
                // that widens them comes back here.
                GroupBounds bounds;
                bounds.set(low, high);
                const uint32_t stopped = groupRun(read.banks, p, end, bounds, sum0, sum1);
                backoff = stopped != p ? 0 : backoff ? (backoff < 8 ? 2 * backoff : 8) : 1;
                exactGroups = backoff;
                at += (stopped - p) * 0x00010001U; // Fewer than 65536 points: no carry between halfwords.
                p = stopped;
                if (end - p < 8) continue;
            }
            // The group it stopped at: without a suspect byte its eight points take the exact path in
            // record order (p..p+3 the low halfwords, p+4..p+7 the high ones); with one, point by point
            // below, where the suspect point ends the run.
            const uint32_t k = p >> 2;
            const uint32_t w0 = load32(b0 + k), w1 = load32(b1 + k), w2 = load32(b2 + k), w3 = load32(b3 + k);
            if (!(suspectPair(w0) | suspectPair(w1) | suspectPair(w2) | suspectPair(w3))) {
                step(uint16_t(w0)); step(uint16_t(w1)); step(uint16_t(w2)); step(uint16_t(w3));
                step(uint16_t(w0 >> 16)); step(uint16_t(w1 >> 16)); step(uint16_t(w2 >> 16)); step(uint16_t(w3 >> 16));
                p += 8;
                continue;
            }
        }
        const uint16_t word = read(p);
        if (__builtin_expect(AdcBusGlitch::suspectWord(word), 0)) break;
        step(word);
        ++p;
    }
    block = {low, high, lowAt, highAt, at, {sum0, sum1}};
    return p;
}
// One point added to a block outside the hot loop (a seed's successor or a corrected point).
inline void pairAdd(uint32_t c, PairBlock &b) {
    extremes(c, b.at, b.low, b.lowAt, b.high, b.highAt);
    b.sum[0] += int16_t(c); b.sum[1] += int32_t(c) >> 16;
    b.at += 0x00010001U;
}
// The running column of one channel, merged block by block in record order.
struct Lane {
    int16_t low = 0, high = 0;
    int32_t sum = 0;
    uint32_t lowAt = 0, highAt = 0;
    void merge(const PairBlock &b, unsigned shift, uint32_t base, bool initial) {
        const int16_t blockLow = int16_t(b.low >> shift), blockHigh = int16_t(b.high >> shift);
        // A later block takes an extreme only by a strict improvement: ties keep the first index.
        if (initial || blockLow < low) { low = blockLow; lowAt = base + uint16_t(b.lowAt >> shift); }
        if (initial || blockHigh > high) { high = blockHigh; highAt = base + uint16_t(b.highAt >> shift); }
        sum += b.sum[shift ? 1 : 0];
    }
};

__attribute__((always_inline)) inline void addPoint(ColumnStats &out, int16_t code) {
    if (!out.count) out.first = out.low = out.high = code;
    if (code < out.low) { out.low = code; out.lowAt = out.count; }
    if (code > out.high) { out.high = code; out.highAt = out.count; }
    out.last = code;
    out.sum += code;
    ++out.count;
}

#if defined(OPENOSC_PAIR_SIMD)
// The four bytes are consecutive points of one channel. Each lane tracks the earliest
// extreme at its phase in the group, with the group number stored in one byte.
struct MonoBlock {
    static constexpr uint32_t Groups = 128; // Two signed bytes/group/halfword sum, down to -32768.
    uint32_t low = 0x7f7f7f7f, high = 0x80808080, lowAt = 0, highAt = 0;
    uint32_t sums = 0, last = 0;

    void merge(ColumnStats &out, uint32_t count) const {
        if (!out.count) out.first = int8_t(first);
        for (unsigned phase = 0; phase < 4; ++phase) {
            const unsigned shift = 8 * phase;
            const int16_t lo = int8_t(low >> shift), hi = int8_t(high >> shift);
            const uint32_t loAt = out.count + 4 * uint8_t(lowAt >> shift) + phase;
            const uint32_t hiAt = out.count + 4 * uint8_t(highAt >> shift) + phase;
            // Different phases are merged out of record order, so ties compare their indexes.
            if ((!out.count && !phase) || lo < out.low || (lo == out.low && loAt < out.lowAt)) {
                out.low = lo; out.lowAt = loAt;
            }
            if ((!out.count && !phase) || hi > out.high || (hi == out.high && hiAt < out.highAt)) {
                out.high = hi; out.highAt = hiAt;
            }
        }
        out.last = int8_t(last >> 24);
        out.sum += int16_t(sums) + (int32_t(sums) >> 16);
        out.count += count;
    }
    uint32_t first = 0;
};

template <class Read, bool Correct> __attribute__((noinline))
uint32_t monoRun(Read read, uint32_t p, uint32_t end, uint32_t correction, MonoBlock &block) {
    uint32_t low = block.low, high = block.high, lowAt = 0, highAt = 0, sums = 0, last = 0;
    uint32_t at = 0;
    for (; p != end; p += 4, at += 0x01010101U) {
        const uint32_t raw = read(p);
        if (__builtin_expect(suspectPair(raw), 0)) break;
        const uint32_t code = Correct ? __qadd8(raw, correction) : raw;
        __SSUB8(code, low); low = __SEL(low, code); lowAt = __SEL(lowAt, at);
        __SSUB8(high, code); high = __SEL(high, code); highAt = __SEL(highAt, at);
        sums = add16(sums, add16(evenCodes(code), oddCodes(code)));
        if (!at) block.first = code;
        last = code;
    }
    block.low = low; block.high = high; block.lowAt = lowAt; block.highAt = highAt;
    block.sums = sums; block.last = last;
    return p;
}

struct MonoBytesRead {
    const uint16_t *const *banks;
    __attribute__((always_inline)) uint32_t operator()(uint32_t p) const {
        const uint32_t k = p >> 2;
        return uint32_t(reinterpret_cast<const uint8_t *>(banks[0])[k]) |
               uint32_t(reinterpret_cast<const uint8_t *>(banks[1])[k]) << 8 |
               uint32_t(reinterpret_cast<const uint8_t *>(banks[2])[k]) << 16 |
               uint32_t(reinterpret_cast<const uint8_t *>(banks[3])[k]) << 24;
    }
};

struct MonoInterleavedRead {
    const uint16_t *const *banks;
    __attribute__((always_inline)) uint32_t operator()(uint32_t p) const {
        const unsigned bank = (p >> 1) & 2;
        const uint32_t k = p >> 3;
        // In capture words the high byte precedes the low one in time.
        const uint32_t words = banks[bank][k] | uint32_t(banks[bank + 1][k]) << 16;
#if defined(__ARM_FEATURE_DSP)
        return __REV16(words);
#else
        return (words & 0xff00ff00U) >> 8 | (words & 0x00ff00ffU) << 8;
#endif
    }
};
#endif
} // namespace

#if defined(OPENOSC_PAIR_SIMD)
template <class Read> __attribute__((noinline))
uint32_t FrameView::Cursor::packedColumn(uint32_t n, ColumnStats &out, Read read, uint32_t correction) {
    const uint32_t ring = frame_.metadata_.ring;
    while (out.count < n) {
        // Neither a physical ring wrap nor the uint32 origin wrap can be inside a group.
        uint32_t points = std::min(n - out.count, ring - position_);
        if (logical_) points = std::min(points, uint32_t(0) - logical_);
        const uint32_t groupPoints = std::min(points & ~3U, 4 * MonoBlock::Groups);
        if (!(position_ & 3) && groupPoints) {
            MonoBlock block;
            const uint32_t end = position_ + groupPoints;
            const uint32_t stopped = correction ? monoRun<Read, true>(read, position_, end, correction, block)
                                                : monoRun<Read, false>(read, position_, end, 0, block);
            const uint32_t reduced = stopped - position_;
            if (reduced) {
                block.merge(out, reduced);
                index_ += reduced; logical_ += reduced; remaining_ -= reduced;
                position_ = !logical_ || stopped == ring ? 0 : stopped;
            }
            if (stopped == end) continue;
            // A possible ADC bus glitch needs the original neighbours and their calibration.
            for (unsigned i = 0; i < 4; ++i) {
                int16_t code = 0;
                next(code);
                addPoint(out, code);
            }
        } else {
            int16_t code = 0;
            next(code);
            addPoint(out, code);
        }
    }
    return n;
}
#endif

__attribute__((noinline)) uint32_t FrameView::Cursor::column(uint32_t n, ColumnStats &out) {
#if defined(OPENOSC_PAIR_SIMD)
    n = std::min(n, remaining_);
    if (n >= 8 && !frame_.split_.active() && !frame_.metadata_.interleaveMatch) {
        if (frame_.metadata_.layout == Layout::FourByteBanks) {
            out = {};
            return packedColumn(n, out, MonoBytesRead{frame_.banks_}, 0);
        }
        if (frame_.metadata_.layout == Layout::InterleavedCh1) {
            const uint32_t correction = uint8_t(frame_.evenShift_) * 0x00010001U;
            out = {};
            return packedColumn(n, out, MonoInterleavedRead{frame_.banks_}, correction);
        }
    }
#endif
    ColumnStats scalar;
    const uint32_t count = run(n, [&](int16_t code) __attribute__((always_inline)) { addPoint(scalar, code); });
    out = scalar;
    return count;
}
// Rare: the canonical bus correction of a suspect byte, exactly as Cursor applies it, out of the
// hot loop so that the loop makes no call and keeps its state in registers.
__attribute__((noinline, cold)) uint32_t FrameView::PairCursor::corrected(uint16_t word, uint32_t index) const {
    uint8_t a = uint8_t(word), b = uint8_t(word >> 8);
    if (AdcBusGlitch::suspect(a)) a = frame_.resolveGlitch(0, index, a);
    if (AdcBusGlitch::suspect(b)) b = frame_.resolveGlitch(1, index, b);
    return codes(a | uint32_t(b) << 16);
}
template <class Read> __attribute__((always_inline)) inline uint32_t FrameView::PairCursor::reduce(uint32_t n, ColumnStats (&out)[2], Read read) {
    if (n > remaining_) n = remaining_;
    out[0] = out[1] = {};
    if (!n) return 0;
    const uint32_t ring = frame_.metadata_.ring;
    const auto pointCodes = [this, &read](uint32_t p, uint32_t index) __attribute__((always_inline)) {
        const uint16_t word = read(p);
        return __builtin_expect(AdcBusGlitch::suspectWord(word), 0) ? corrected(word, index) : codes(spread(word));
    };
    Lane lanes[2];
    uint32_t first = 0, last = 0; // Both channels' codes of the column's first and last points.
    for (uint32_t done = 0; done < n;) {
        const uint32_t size = n - done < 65535U ? n - done : 65535U;
        PairBlock block;
        uint32_t left = size;
        bool seeded = false;
        while (left) {
            // A run of positions with no ring end and no uint32 origin wrap inside it, so the hot
            // loop only steps the position. The wrap distance needs 64 bits when logical_ is 0.
            const uint64_t toWrap = (uint64_t(1) << 32) - logical_;
            uint32_t run = ring - position_;
            if (run > left) run = left;
            if (toWrap < run) run = uint32_t(toWrap);
            const uint32_t start = position_, end = start + run;
            uint32_t p = start;
            if (!seeded) { // The block's first point seeds both lanes outside the loop.
                const uint32_t c = pointCodes(p++, index_);
                block = {c, c, 0, 0, 0x00010001U, {int16_t(c), int32_t(c) >> 16}};
                if (!done) first = c;
                seeded = true;
            }
            while ((p = pairRun(read, p, end, block)) != end) { // A suspect point: the exact path, then on.
                pairAdd(corrected(read(p), index_ + (p - start)), block);
                ++p;
            }
            left -= run; index_ += run; remaining_ -= run;
            const uint32_t logical = logical_ + run; // Wraps exactly at the run's end, if at all.
            logical_ = logical;
            position_ = logical == 0 || end == ring ? 0 : end;
            if (done + size == n && !left) last = pointCodes(end - 1, index_ - 1); // Once per column.
        }
        for (unsigned c = 0; c < 2; ++c) lanes[c].merge(block, 16 * c, done, !done);
        done += size;
    }
    for (unsigned c = 0; c < 2; ++c) {
        const auto &l = lanes[c];
        out[c] = {int16_t(first >> (16 * c)), int16_t(last >> (16 * c)), l.low, l.high, l.sum, l.lowAt, l.highAt, n};
    }
    return n;
}
__attribute__((noinline)) uint32_t FrameView::PairCursor::column(uint32_t n, ColumnStats (&out)[2]) {
    const auto &f = frame_;
    if (f.split_.historyWords) { // Each word through the split mapping; the age is resolved per point.
        const uint32_t lane = f.laneRing(); // Invariant of the record: no division per word.
        return reduce(n, out, [&f, lane](uint32_t p) __attribute__((always_inline)) { return f.split_.word(f.banks_, p & 3, p >> 2, lane); });
    }
    if (f.metadata_.layout == Layout::FourBanks)
        return reduce(n, out, FourBanksRead{{f.banks_[0], f.banks_[1], f.banks_[2], f.banks_[3]}});
    const uint16_t *bank = f.banks_[0];
    return reduce(n, out, [bank](uint32_t p) __attribute__((always_inline)) { return bank[p]; });
}
bool FrameView::PairCursor::next(int16_t &ch1, int16_t &ch2) {
    if (!remaining_) return false;
    const auto &f = frame_;
    const uint32_t p = position_;
    const uint16_t word = f.wordAt(p);
    const uint32_t c = __builtin_expect(AdcBusGlitch::suspectWord(word), 0) ? corrected(word, index_) : codes(spread(word));
    ch1 = int16_t(c); ch2 = int16_t(int32_t(c) >> 16);
    --remaining_; ++index_;
    if (++logical_ == 0 || ++position_ == f.metadata_.ring) position_ = 0;
    return true;
}
FrameView FrameView::withTrigger(bool found, uint32_t index, TriggerEdge edge) const {
    const bool same = found && metadata_.triggered && index == metadata_.triggerIndex;
    if (same && metadata_.triggerEdge.known) edge = metadata_.triggerEdge; // The capture's own.
    FrameView view = *this;
    view.metadata_.clearTrigger();
    view.metadata_.triggered = found;
    view.metadata_.triggerIndex = found ? index : 0;
    if (same) {
        view.metadata_.triggerShift = metadata_.triggerShift; view.metadata_.eventOffset = metadata_.eventOffset;
        view.metadata_.eventKnown = metadata_.eventKnown;
    }
    if (found) view.metadata_.triggerEdge = edge;
    view.valid_ = view.validate();
    return view;
}
FrameView FrameView::withPretrigger(uint32_t point) const {
    FrameView view = *this;
    view.metadata_.pretrigger = point;
    view.metadata_.pretriggerKnown = true;
    view.valid_ = view.validate();
    return view;
}
FrameView FrameView::withShiftedTrigger(uint32_t index, int32_t shift, TriggerEdge edge) const {
    FrameView view = *this;
    view.metadata_.clearTrigger();
    view.metadata_.triggered = true;
    view.metadata_.triggerIndex = index;
    view.metadata_.triggerShift = shift;
    view.metadata_.triggerEdge = edge;
    view.valid_ = view.validate();
    return view;
}
} // namespace openosc::scope
