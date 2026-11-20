#include "openosc/calibration/interleave_match_wizard.hpp"
#include <algorithm>
#include <cmath>
#include <complex>

namespace openosc::calibration {
namespace {
constexpr double Tau = 6.2831853071795864769;
using Complex = std::complex<double>;
using Match = scope::InterleaveMatch;
using Issue = InterleaveMatchWizard::Issue;

struct ToneFit {
    Complex phasor;
    double residual;
    bool valid;
};
// Least squares of code = DC + a*cos + b*sin. The covariance terms remove DC
// and account for partial periods and the reduced sample grid of either ADC.
struct Quadrature {
    double x = 0, c = 0, s = 0, xx = 0, cc = 0, ss = 0, cs = 0, xc = 0, xs = 0;
    unsigned count = 0;
    void add(double code, double cosine, double sine) {
        x += code; c += cosine; s += sine;
        xx += code * code; cc += cosine * cosine; ss += sine * sine;
        cs += cosine * sine; xc += code * cosine; xs += code * sine;
        ++count;
    }
    ToneFit finish() const {
        if (!count) return {};
        const double ac = cc - c * c / count, as = ss - s * s / count, cross = cs - c * s / count;
        const double i = xc - x * c / count, q = xs - x * s / count;
        const double det = ac * as - cross * cross;
        if (det < .01 * double(count) * count) return {};
        const double a = (as * i - cross * q) / det, b = (ac * q - cross * i) / det;
        return {{a, -b}, std::sqrt(std::max(0.0, (xx - x * x / count - a * i - b * q) / count)), true};
    }
};

bool correlate(const scope::FrameView &frame, uint32_t start, uint32_t points,
               double omega, ToneFit (&tone)[2], bool checkLevel) {
    Quadrature lane[2]{};
    const double dc = std::cos(omega), ds = std::sin(omega);
    double c = 1, s = 0;
    const unsigned first = (frame.metadata().origin + start) & 1;
    bool clipped = false;
    frame.cursor(scope::Channel::Ch1, start).run(points, [&](int16_t code) {
        clipped |= code <= -124 || code >= 123;
        lane[(first + lane[0].count + lane[1].count) & 1].add(code, c, s);
        const double next = c * dc - s * ds;
        s = s * dc + c * ds; c = next;
    });
    tone[0] = lane[0].finish(); tone[1] = lane[1].finish();
    return !(checkLevel && clipped) && tone[0].valid && tone[1].valid;
}

// Phase over two windows estimates the generator's actual frequency. The coarse baseline
// is short enough that a 0.5% setting error cannot wrap at the highest requested frequency.
bool refine(const scope::FrameView &frame, uint32_t width, double &omega) {
    ToneFit first[2], second[2];
    if (!correlate(frame, 0, width, omega, first, false) ||
        !correlate(frame, width, width, omega, second, false) ||
        std::abs(first[1].phasor) < 6 || std::abs(second[1].phasor) < 6) return false;
    const double phase = std::arg(second[1].phasor * std::conj(first[1].phasor));
    omega += std::remainder(phase - width * omega, Tau) / width;
    return true;
}

bool clean(const ToneFit &fit) {
    return fit.residual <= std::max(1.5, .05 * std::abs(fit.phasor));
}
double imageDb(const ToneFit (&tones)[2]) {
    const Complex a = tones[1].phasor, b = tones[0].phasor;
    return 20 * std::log10(std::max(1e-9, std::abs(a - b) / std::max(1e-9, std::abs(a + b))));
}
} // namespace

void InterleaveMatchWizard::begin(uint32_t clockHz, const scope::FrameMetadata &metadata) {
    *this = {};
    clockHz_ = clockHz;
    rate_ = metadata.samplesPerSecond;
    channel_ = metadata.channels[0];
}
bool InterleaveMatchWizard::sameSetup(uint32_t clockHz, const scope::FrameMetadata &m) const {
    const auto &c = m.channels[0];
    return clockHz == clockHz_ && m.layout == scope::Layout::InterleavedCh1 && !m.rolling &&
           m.samples >= Points && m.samplesPerSecond.numerator == rate_.numerator &&
           m.samplesPerSecond.denominator == rate_.denominator && c.calibrated &&
           c.calibrationCrc == channel_.calibrationCrc && c.voltsPerCode == channel_.voltsPerCode &&
           c.zeroCode == channel_.zeroCode && c.millivoltsPerDivision == channel_.millivoltsPerDivision &&
           c.coupling == channel_.coupling && c.analogPosition == channel_.analogPosition &&
           c.probeNumerator == channel_.probeNumerator && c.probeDenominator == channel_.probeDenominator;
}
unsigned InterleaveMatchWizard::frequencyMHz() const {
    // The same references at 180/200/280 MS/s, away from Fs/4 and Fs/2 on each clock.
    constexpr unsigned Frequencies[] = {2, 12, 24, 39, 54, 66, 6, 17, 31, 42, 60};
    static_assert(sizeof Frequencies / sizeof *Frequencies == Count);
    return index_ < Count ? Frequencies[index_] : 0;
}
void InterleaveMatchWizard::startTone(uint32_t now, uint64_t timeUs) {
    if (step_ != Step::Reference) return;
    at_ = now;
    afterUs_ = timeUs + SettleMs * 1000;
    issue_ = Issue::None;
    step_ = Step::Measuring;
}
void InterleaveMatchWizard::noFrame() {
    if (step_ == Step::Measuring) retry(Issue::NoFrame);
}
void InterleaveMatchWizard::take(const scope::FrameView &frame) {
    if (step_ != Step::Measuring || !frame.valid() || !sameSetup(clockHz_, frame.metadata())) return;
    const auto original = frame.withInterleaveMatch(nullptr);
    const double target = Tau * frequencyMHz() * 1e6 / rate_.value();
    double omega = target;
    ToneFit tone[2];
    if (!refine(original, 256, omega)) { retry(Issue::Level); return; }
    if (std::abs(omega - target) > .006 * target || !refine(original, 2048, omega) ||
        std::abs(omega - target) > .005 * target) { retry(Issue::WrongFrequency); return; }
    if (!correlate(original, 0, Points, omega, tone, true) ||
        std::min(std::abs(tone[0].phasor), std::abs(tone[1].phasor)) < 12) { retry(Issue::Level); return; }
    if (!clean(tone[0]) || !clean(tone[1])) { retry(Issue::Impure); return; }

    if (index_ < Training) {
        const Complex ratio = tone[1].phasor / tone[0].phasor;
        training_[index_++] = {float(omega), float(ratio.real()), float(ratio.imag())};
        if (index_ == Training && !fit()) fail(Issue::Fit);
        else step_ = Step::Reference;
        return;
    }
    const auto corrected = frame.withInterleaveMatch(&filter_);
    ToneFit check[2];
    if (!correlate(corrected, Match::Radius, Points - 2 * Match::Radius, omega, check, true)) {
        retry(Issue::Level); return;
    }
    // Compare the same points before and after, including the deployed filter's rounding.
    correlate(original, Match::Radius, Points - 2 * Match::Radius, omega, tone, false);
    acceptVerification(imageDb(tone), imageDb(check));
}
void InterleaveMatchWizard::acceptVerification(double before, double after) {
    if (after > std::max(before + 1, -52.0)) { fail(Issue::Validation); return; }
    improvement_[index_++ - Training] = float(before - after);
    if (index_ < Count) { step_ = Step::Reference; return; }
    std::sort(improvement_, improvement_ + Verification);
    if (improvementDb() < 3) fail(Issue::Validation);
    else step_ = Step::Review;
}
bool InterleaveMatchWizard::fit() {
    constexpr unsigned Taps = Match::Taps;
    double equation[Taps][Taps + 1]{};
    for (unsigned j = 0; j < Taps; ++j)
        for (unsigned k = 0; k < Taps; ++k)
            equation[j][k] = ((j + k) & 1 ? -1 : 1) * .35 * .35 + (j == k ? .25 : 0);
    for (const auto &tone : training_) {
        Complex row[Taps];
        const Complex ratio(tone.real, tone.imag), target = 1.0 - ratio;
        for (unsigned k = 0; k < Taps; ++k)
            row[k] = std::polar(1.0, double(tone.omega) * (int(k) - int(Match::Radius))) *
                     ((k & 1) ? ratio : Complex(1, 0));
        for (unsigned j = 0; j < Taps; ++j) {
            for (unsigned k = 0; k < Taps; ++k)
                equation[j][k] += std::real(std::conj(row[j]) * row[k]);
            equation[j][Taps] += std::real(std::conj(row[j]) * target);
        }
    }
    // Ridge regularization makes the normal matrix positive definite, including
    // where a measured frequency leaves the imaginary part nearly redundant.
    for (unsigned j = 0; j < Taps; ++j) {
        const double pivot = equation[j][j];
        if (!(pivot > 0)) return false;
        for (unsigned k = j; k <= Taps; ++k) equation[j][k] /= pivot;
        for (unsigned i = 0; i < Taps; ++i) {
            if (i == j) continue;
            const double factor = equation[i][j];
            for (unsigned k = j; k <= Taps; ++k) equation[i][k] -= factor * equation[j][k];
        }
    }
    for (unsigned j = 0; j < Taps; ++j) {
        const double value = equation[j][Taps] * Match::Unity;
        if (!std::isfinite(value) || std::abs(value) > Match::MaximumNorm) return false;
        filter_.q15[j] = int16_t(std::lround(value));
    }
    return filter_.valid();
}
} // namespace openosc::calibration
