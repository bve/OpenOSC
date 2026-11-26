#include "openosc/acquisition/trend.hpp"
#include "openosc/acquisition/arena.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <numeric>

namespace openosc::scope {
// Record bank 3: the means (the DMA source of the published frame), the live record, the copy on show.
struct TrendBuffer::Memory {
    uint16_t means[Capacity]; // CH1 in the low byte, CH2 in the high one, as Roll's history.
    TrendPoints live, shown;
};
namespace {
// The mean of `n` points summing to `sum`, rounded half away from zero as RollingBuffer does; in 64 bits,
// as the rounding takes a sum of MaximumPerPoint codes of -128 (-2^31) below the int32 range.
uint8_t meanCode(int32_t sum, uint32_t n) {
    const int64_t half = n / 2;
    return uint8_t(int8_t((sum < 0 ? sum - half : sum + half) / int64_t(n)));
}
uint16_t saturate(uint32_t value) { return uint16_t(std::min<uint32_t>(value, UINT16_MAX)); }
// `base` per `perPoint`: exact while it fits 32 bits, else (a record of months: the denominator passes
// 32 bits) numerator and denominator halve together until both fit, which keeps the rate within a
// relative 1 / numerator + 1 / denominator (those of the halved rate) of it.
Rational pointRate(const Rational &base, uint32_t perPoint) {
    Rational rate;
    if (base.scaled(1, perPoint, rate) || !base.valid() || !perPoint) return rate;
    uint64_t n = base.numerator, d = uint64_t(base.denominator) * perPoint;
    const uint64_t common = std::gcd(n, d);
    n /= common; d /= common;
    while (n > UINT32_MAX || d > UINT32_MAX) { n >>= 1; d >>= 1; }
    return {uint32_t(n), uint32_t(d)};
}
} // namespace
bool TrendView::values(const FrameMetadata &metadata, uint32_t index, float (&out)[9]) const {
    if (!points || index >= size) return false;
    for (unsigned c = 0; c < 2; ++c) {
        const auto &channel = metadata.channels[c];
        float *value = out + 3 * c;
        value[0] = value[1] = value[2] = NAN;
        if (!channel.enabled || !channel.calibrated) continue;
        const double scale = channel.voltsPerCode * channel.probeNumerator / channel.probeDenominator;
        value[0] = float((points->low[c][index] - channel.zeroCode) * scale);
        value[1] = float((double(points->sum[c][index]) / perPoint - channel.zeroCode) * scale);
        value[2] = float((points->high[c][index] - channel.zeroCode) * scale);
    }
    const bool reading = points->dmmCount[index];
    out[6] = reading ? points->dmmLow[index] : NAN;
    out[7] = reading ? points->dmmMean[index] : NAN;
    out[8] = reading ? points->dmmHigh[index] : NAN;
    return true;
}
void TrendView::column(unsigned x, unsigned width, uint32_t &first, uint32_t &last) const {
    const int64_t missing = int64_t(window) - size; // Window positions before the first point.
    const auto at = [&](unsigned edge) { return std::clamp<int64_t>(int64_t(edge) * window / width - missing, 0, size); };
    first = uint32_t(at(x));
    last = uint32_t(at(x + 1));
}
unsigned TrendView::before(unsigned width) const {
    uint32_t first, last;
    unsigned count = 0;
    while (count < width && (column(count, width, first, last), first >= last)) ++count;
    return count;
}
TrendBuffer::TrendBuffer(uint16_t *memory) : memory_(new (memory) Memory) {
    static_assert(sizeof(Memory) <= AcquisitionArena::BankBytes, "A trend and its copy fit one record bank");
    shown_.points = &memory_->shown;
}
void TrendBuffer::reset(Quantity quantity) {
    active_ = true; started_ = false; copyAll_ = true;
    size_ = shownSize_ = pending_ = 0;
    perPoint_ = 1;
    full_ = false;
    sum_[0] = sum_[1] = 0; low_[0] = low_[1] = high_[0] = high_[1] = 0;
    dmmSum_ = 0; dmmLow_ = dmmHigh_ = 0; dmmCount_ = dmmMissing_ = 0;
    quantity_ = quantity;
    shown_.sequence = 0; // The frame on show is not this record's; what it holds stays for a draw under way.
}
void TrendBuffer::forget() {
    active_ = false;
    shown_.sequence = 0;
}
bool TrendBuffer::append(const FrameView &frame) {
    if (!active_ || full_) return true;
    const auto &metadata = frame.metadata();
    if (!started_) { startUs_ = metadata.timestampUs; started_ = true; }
    for (size_t i = 0; i < metadata.samples; ++i) {
        for (unsigned c = 0; c < 2; ++c) {
            uint8_t raw;
            if (!metadata.channels[c].enabled) continue;
            if (!frame.code(static_cast<Channel>(c), i, raw)) return false;
            const int8_t code = int8_t(raw);
            sum_[c] += code;
            if (!pending_ || code < low_[c]) low_[c] = code;
            if (!pending_ || code > high_[c]) high_[c] = code;
        }
        if (++pending_ == perPoint_) close();
    }
    return true;
}
void TrendBuffer::close() {
    auto &live = memory().live;
    const uint32_t index = size_;
    uint8_t mean[2];
    for (unsigned c = 0; c < 2; ++c) {
        live.sum[c][index] = sum_[c];
        live.low[c][index] = low_[c];
        live.high[c][index] = high_[c];
        mean[c] = meanCode(sum_[c], perPoint_);
        sum_[c] = 0; low_[c] = high_[c] = 0;
    }
    memory().means[index] = uint16_t(mean[0] | mean[1] << 8);
    live.dmmCount[index] = dmmCount_;
    live.dmmMissing[index] = dmmMissing_;
    live.dmmMean[index] = dmmCount_ ? float(dmmSum_ / dmmCount_) : 0;
    live.dmmLow[index] = dmmCount_ ? dmmLow_ : 0;
    live.dmmHigh[index] = dmmCount_ ? dmmHigh_ : 0;
    dmmSum_ = 0; dmmLow_ = dmmHigh_ = 0; dmmCount_ = dmmMissing_ = 0;
    pending_ = 0;
    ++size_;
    ++revision_;
    if (size_ < Capacity) return;
    if (perPoint_ < MaximumPerPoint) halve();
    else full_ = true;
}
// Every pair of points becomes one covering twice the samples: the sums add, the extremes combine,
// the DMM means weigh by their readings and the counts add (a point's readings fit 32 bits: the DMM's
// 9600-baud UART brings at most 53 a second, a point spans at most MaximumPerPoint samples at the
// trend's slowest 164 S/s, 102 400 s: 5.5 million). In place, upwards: point i reads 2i and 2i + 1 only.
void TrendBuffer::halve() {
    auto &live = memory().live;
    const uint32_t points = 2 * perPoint_;
    for (uint32_t i = 0; i < Capacity / 2; ++i) {
        const uint32_t a = 2 * i, b = a + 1;
        uint8_t mean[2];
        for (unsigned c = 0; c < 2; ++c) {
            live.sum[c][i] = live.sum[c][a] + live.sum[c][b];
            live.low[c][i] = std::min(live.low[c][a], live.low[c][b]);
            live.high[c][i] = std::max(live.high[c][a], live.high[c][b]);
            mean[c] = meanCode(live.sum[c][i], points);
        }
        memory().means[i] = uint16_t(mean[0] | mean[1] << 8);
        const uint32_t ca = live.dmmCount[a], cb = live.dmmCount[b], count = ca + cb;
        float low = 0, high = 0, average = 0;
        if (count) {
            average = float((double(live.dmmMean[a]) * ca + double(live.dmmMean[b]) * cb) / count);
            low = !ca ? live.dmmLow[b] : !cb ? live.dmmLow[a] : std::min(live.dmmLow[a], live.dmmLow[b]);
            high = !ca ? live.dmmHigh[b] : !cb ? live.dmmHigh[a] : std::max(live.dmmHigh[a], live.dmmHigh[b]);
        }
        const uint32_t missing = uint32_t(live.dmmMissing[a]) + live.dmmMissing[b];
        live.dmmMean[i] = average; live.dmmLow[i] = low; live.dmmHigh[i] = high;
        live.dmmCount[i] = count; live.dmmMissing[i] = saturate(missing);
    }
    perPoint_ = points;
    size_ = Capacity / 2;
    copyAll_ = true;
}
// The reading joins the point open when it arrives (up to one point span late).
void TrendBuffer::reading(float value) {
    if (!active_) return;
    if (value != value) { dmmMissing_ = saturate(uint32_t(dmmMissing_) + 1); return; }
    if (!dmmCount_ || value < dmmLow_) dmmLow_ = value;
    if (!dmmCount_ || value > dmmHigh_) dmmHigh_ = value;
    dmmSum_ += value;
    ++dmmCount_;
}
void TrendBuffer::meterGap() {
    if (active_) dmmMissing_ = saturate(uint32_t(dmmMissing_) + 1);
}
void TrendBuffer::restartMeter(Quantity quantity) {
    if (!active_) return;
    auto &live = memory().live;
    std::memset(live.dmmCount, 0, size_ * sizeof *live.dmmCount);
    std::memset(live.dmmMissing, 0, size_ * sizeof *live.dmmMissing);
    dmmSum_ = 0; dmmLow_ = dmmHigh_ = 0; dmmCount_ = dmmMissing_ = 0;
    quantity_ = quantity;
    copyAll_ = true;
    ++revision_;
}
FrameView TrendBuffer::snapshot(const FrameMetadata &base) const {
    const Rational rate = pointRate(base.samplesPerSecond, perPoint_);
    if (!active_ || !started_ || !size_ || !rate.valid()) return {};
    auto metadata = base;
    metadata.layout = Layout::Linear;
    metadata.interleaveOffset = 0;
    metadata.rolling = true;
    metadata.rollPhase = FrameMetadata::NoRollPhase; // Merged points: the base's phase is the Roll ring's.
    metadata.clearTrigger();
    metadata.samples = size_;
    // The window is the record, once the points have merged: it fills the plot, so the time/div grows with
    // the record. Before the first merge the record fills the plot from the right, Capacity points wide.
    const uint32_t window = windowPoints();
    metadata.ring = window;
    metadata.origin = 0;
    metadata.samplesPerSecond = rate;
    const double period = double(base.samplesPerSecond.denominator) / base.samplesPerSecond.numerator;
    metadata.timebaseNs = uint64_t(std::llround(double(window) * perPoint_ * period * 1e8));
    // Each point lies at the middle of the samples it sums, as RollingBuffer::snapshot's do.
    metadata.timestampUs = startUs_ + uint64_t(std::llround(double(perPoint_ - 1) / 2 * period * 1e6));
    const uint16_t *banks[] = {memory().means, nullptr, nullptr, nullptr};
    return {metadata, banks, Capacity};
}
void TrendBuffer::publish(const FrameMetadata &published) {
    auto &live = memory().live;
    auto &shown = memory().shown;
    const uint32_t first = copyAll_ || shownSize_ > size_ ? 0 : shownSize_, count = size_ - first;
    for (unsigned c = 0; c < 2; ++c) {
        std::memcpy(shown.sum[c] + first, live.sum[c] + first, count * sizeof *live.sum[c]);
        std::memcpy(shown.low[c] + first, live.low[c] + first, count * sizeof *live.low[c]);
        std::memcpy(shown.high[c] + first, live.high[c] + first, count * sizeof *live.high[c]);
    }
    std::memcpy(shown.dmmLow + first, live.dmmLow + first, count * sizeof *live.dmmLow);
    std::memcpy(shown.dmmHigh + first, live.dmmHigh + first, count * sizeof *live.dmmHigh);
    std::memcpy(shown.dmmMean + first, live.dmmMean + first, count * sizeof *live.dmmMean);
    std::memcpy(shown.dmmCount + first, live.dmmCount + first, count * sizeof *live.dmmCount);
    std::memcpy(shown.dmmMissing + first, live.dmmMissing + first, count * sizeof *live.dmmMissing);
    shownSize_ = size_;
    copyAll_ = false;
    shown_.size = size_;
    shown_.perPoint = perPoint_;
    shown_.window = published.ring;
    shown_.rate = published.samplesPerSecond;
    shown_.timestampUs = published.timestampUs;
    shown_.startUs = startUs_;
    shown_.quantity = quantity_;
    shown_.sequence = published.sequence;
}
} // namespace openosc::scope
